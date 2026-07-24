# 小乔运费结算 性能优化路线图（提速方案留存）

> 生成日期：2026-07-24
> 当前程序版本：1.0.0
> 当前测试环境：Apple M? arm64 / 8GB 内存 / 8 逻辑核 / DuckDB 1.5.4 / Qt 6.5+
> 基准数据：`/Users/cxd/帐单/` 下 4 份帐单合计 **2,629,906 行**，端到端总耗时 **44.1 秒**，吞吐 **≈ 59,600 行/秒**（含 xlsx 读 + 算 + xlsx 写）

---

## 0. 当前性能基线（务必保留作对比）

| 源文件 | 行数 | 耗时（端到端） | 吞吐（行/秒） |
|--------|-----:|---------------:|-------------:|
| 珀莱雅-4月发件账单.xlsx | 751,800 | 12.9 s | 58,300 |
| 蜜丝婷-4月发件账单表1.xlsx | 962,522 | 15.9 s | 60,500 |
| 蜜丝婷-4月发件账单表2.xlsx | 419,265 | 6.9 s | 60,800 |
| 蜜丝婷-5月发件账单.xlsx | 496,319 | 8.4 s | 59,100 |
| **平均** | - | - | **≈ 59,700** |

阶段耗时分解（以 96 万行为例，15.9s 总耗时）：

```
读 Excel (read_xlsx + 解析)   6.4s   40%   ← IO+解压瓶颈
10 层 CTE SQL 运费计算        7.1s   45%   ← CPU 向量化计算
写 Excel (COPY FORMAT xlsx)   2.4s   15%   ← xlsx zip 压缩
--------------------------------------------
合计                          15.9s  100%
```

## 1. 方案一：Parquet 热缓存 + 并行 xlsx Reader（预估 +30~40%，最快落地）

**收益预估**：100 万行端到端从 16s → **10~11s**，首读慢，同文件二次运行立减 60% 读时间。

### 1.1 改造思路
- `DuckDBManager::ImportFromFile()` 中：
  1. 输入为 `.xlsx/.csv` 时，首次导入后 `COPY _input_tmp TO '缓存.parquet' (FORMAT PARQUET)`；
  2. 以**文件绝对路径 + mtime + size**做 key，存在 `QCache<QString, QString>`（50 项 LRU）；
  3. 下次再跑同一文件时，`read_parquet` 代替 `read_xlsx`（列存+零解压，读 100 万行 ≈ 0.4s）。
- 利用 DuckDB 并行化参数：
  ```sql
  SET threads = 7;
  SET preserve_insertion_order = false;  -- 允许算子重排
  SET enable_progress_bar = false;
  ```
  目前 `AppConfig::ApplyAutoPerformance()` 已在内存/线程数上配置，但 DuckDBManager 里尚未 `PRAGMA/SET` 这 3 条（见 2.2 代码点）。

### 1.2 涉及文件
- [duckdb_manager.cpp](file:///Users/cxd/duckdb/xiaoqiao_freight/src/db/duckdb_manager.cpp) — `Init()` 增加 SET 语句；`ImportFromFile()` 增加 Parquet 缓存分支
- [app_config.cpp](file:///Users/cxd/duckdb/xiaoqiao_freight/src/core/app_config.cpp#L81-L86) — 新增 `GetCacheDir()` 调用路径（已有方法，直接复用）

### 1.3 实施步骤
1. 在 `DuckDBManager::Init()` 末尾 `con.Query("SET threads = N; SET preserve_insertion_order=false; ...")`，N 取 `AppConfig::Instance().GetThreadCount()`
2. 给 ImportFromFile 前加 3 行 `std::call_once` 初始化缓存 map
3. 写 `QString ParquetCachePathFor(const QString &src)`，哈希到 `AppConfig::GetCacheDir() + "/<md5>.parquet"`
4. 命中缓存 → `CREATE TABLE t AS SELECT * FROM read_parquet(...)`；未命中 → 原逻辑 + 额外 COPY 写 parquet

### 1.4 风险/副作用
- Parquet 体积 ≈ xlsx 的 1/5，但 100 份历史帐单累计会占 2~5GB；建议在 AppConfig 里加 `max_cache_mb`（默认 2GB）+ 启动时 LRU 清理
- 若用户 Excel 改了内容但 mtime/size 没变（概率极低），可能读脏数据 → key 加文件头部 4KB SHA1 可完全避免

---

## 2. 方案二：子查询改 LATERAL JOIN + 预物化维度表（预估 +60~80%）

**收益预估**：只改 SQL，不依赖缓存；100 万行端到端 16s → **3~4s**；CPU 占用从 700% 拉满到 780%。

### 2.1 改造思路
当前 `BuildCalcSQL()` 在 CTE 末尾的 3 处"相关子查询 + SELECT SUM(...) FROM xxx WHERE xxx = rac.xxx"，对大表会走 nested-loop（行数 × 维度表行数 = 96 万 × 288 ≈ 2.7 亿次 inner loop）：

- `remote_areas` 相关子查询（`remote_surcharge` 列）
- `surcharge_strategies` 相关子查询（`strategy_surcharge` 列）
- `fuel_surcharge` 当前是 `LEFT JOIN + effective_date`，已较好，但仍可改成 scalar subquery-first

改成 **CTE 预聚合 + LATERAL**：

```sql
-- 改造前（当前写法）：
COALESCE((SELECT SUM(ra.surcharge) FROM remote_areas ra WHERE ... = fsc.dest_province), 0)

-- 改造后（推荐写法）：
remote_areas_by_template AS (
  SELECT template_id, province, city, district, SUM(surcharge) AS remote_surcharge
  FROM   remote_areas
  WHERE  is_active = 1
  GROUP  BY template_id, province, city, district
),
base_fee_calc AS (...),
remote_area_calc AS (
  SELECT bfc.*, COALESCE(ra.remote_surcharge, 0) AS remote_surcharge
  FROM   base_fee_calc bfc
  LEFT   JOIN remote_areas_by_template ra
    ON  ra.template_id = bfc.template_id
    AND REGEXP_REPLACE(ra.province,...) = REGEXP_REPLACE(bfc.dest_province,...)
    AND (ra.city IS NULL OR ra.city = '' OR ra.city = bfc.dest_city)
    AND (ra.district IS NULL OR ra.district = '')
)
```

`surcharge_strategies` 同理：把 4 类 strategy_scope（global/province/customer/全局）先 UNPIVOT 成 `(template_id, customer_id, province, effect_start, effect_end, strategy_surcharge_per_row)` 的宽表，再 LEFT JOIN 一次。

### 2.2 涉及文件
- [calc_service.cpp](file:///Users/cxd/duckdb/xiaoqiao_freight/src/services/calc_service.cpp#L264-L442) — `BuildCalcSQL()`：
  - 把 `remote_area_calc` 的子查询替换成预聚合 CTE
  - 把 `strategy_surcharge_calc` 的子查询替换成 **customer_scope UNION province_scope UNION global_scope** 然后单 LEFT JOIN
- 同步替换 `CalcSingle()` 的 3 处相关子查询（保持 SQL 形状一致，逻辑不变）：[calc_service.cpp#L94-L164](file:///Users/cxd/duckdb/xiaoqiao_freight/src/services/calc_service.cpp#L94-L164)

### 2.3 验证方法
用 batch_runner 单独跑 96 万行那份：
```bash
time ./build/bin/batch_runner -o /tmp/t1 /Users/cxd/帐单/蜜丝婷-4月发件账单表1.xlsx
# 改造前基准：15.9s
# 改造后目标：<6s（SQL 部分 7.1s → 1.5~2s）
```

### 2.4 风险/副作用
- SQL 会从 200 行变成 350 行，维护成本上升 → 保留旧 SQL 作为 `BuildCalcSQL_LoopJoin()` 在 Debug 模式下做结果 diff 校验（双写一致性）
- 偏远地区/附加策略是"一对多 SUM"，必须小心 GROUP BY 维度不重复 → 单测至少覆盖 3 条样例

---

## 3. 方案三：列式 Parquet 主路径 + xlsx 延迟导出（预估 +200~300%，百万行 2~3 秒）

**收益预估**：对"内部算帐+回库"场景完全替代 xlsx，100 万行端到端 **<3 秒**；对外发 Excel 时后台转码。

### 3.1 改造思路
- `batch_runner` / `CalcFromFile` 默认输出改成 **parquet**（用户界面里菜单"导出→格式"选项保留 xlsx）：
  - COPY 写 Parquet 速度是 xlsx 的 **5~10×**，且文件更小（压缩率 1:10 vs 1:3）
- UI 侧增加一个"生成对外 xlsx"按钮（非阻塞异步，调用后台线程），用户不感知等待：
  ```cpp
  // duckdb 一条 SQL 同时产出两种格式
  COPY result TO '结果.parquet' (FORMAT PARQUET);  // 0.3s 阻塞主线
  // QtConcurrent 异步
  COPY result TO '结果.xlsx'  (FORMAT xlsx);       // 2.4s 后台做
  ```
- 把列头中文字段名缓存为 Parquet 的 `field_id` 元数据，Excel 打开时用一个 VIEW 映射回去，避免列头对不上。

### 3.2 涉及文件
- [batch_calc_dialog.cpp](file:///Users/cxd/duckdb/xiaoqiao_freight/src/ui/dialogs/batch_calc_dialog.cpp#L498-L502) — ExportToFile 默认改 `.parquet`，保留"另存为 xlsx"异步任务
- [duckdb_manager.cpp](file:///Users/cxd/duckdb/xiaoqiao_freight/src/db/duckdb_manager.cpp#L322-L349) — `ExportToFile()` 里 parquet 分支走阻塞，xlsx 分支返回一个 `std::future<bool>`（新增重载）

### 3.3 风险/副作用
- Parquet 不能被普通财务人员直接双击打开（必须转 xlsx）→ 默认给用户发的仍然走"异步 xlsx"；Parquet 只是**后台计算主路径**
- 历史批次用 Parquet 时，需要在 `HistoryService` 里存 parquet 路径，避免旧结果 xlsx 找不到

---

## 4. 方案选择决策表

| 指标 | 方案一（Parquet 缓存） | 方案二（SQL 改写） | 方案三（Parquet 主路径） |
|------|----------------------|-------------------|------------------------|
| 预估收益 | +30~40% | +60~80% | +200~300% |
| 改造工作量 | 0.5 天 | 1.5~2 天 | 2~3 天 |
| 对现有 SQL/UI 侵入 | 低（不改 SQL） | 中（改 300 行 SQL） | 高（导出路径全改） |
| 需要新增测试 | 缓存命中/失效 3 条用例 | 3 条结果 diff 用例 | 异步任务/历史回查 |
| 首份帐单加速 / 二次加速 | 首份+15% / 二次+50% | 每份都 +60~80% | 每份都 +200~300% |

**推荐落地顺序**：**方案一（先拿便宜的 40%）→ 方案二（拿核心 SQL 的 80%）→ 方案三（如未来帐单 > 2000 万行/月时再上）**

---

## 5. 统一验证脚本（每次改完跑一次）

以下命令均在项目根目录执行，比较改造前后 batch_runner 的 wall time：

```bash
cd /Users/cxd/duckdb/xiaoqiao_freight
cmake -S . -B build -DCMAKE_PREFIX_PATH=/opt/homebrew/opt/qt6 >/dev/null
make -C build -j$(sysctl -n hw.ncpu) batch_runner xiaoqiao_freight 2>&1 | tail -5

mkdir -p /tmp/xq_bench
rm -rf /tmp/xq_bench/*
INPUT_DIR="/Users/cxd/帐单"
for f in "$INPUT_DIR"/*.xlsx; do
  echo ">>> $f"
  command time -f 'real=%e user=%U sys=%S maxmem=%MKB' \
    ./build/bin/batch_runner -o /tmp/xq_bench "$f" 2>&1 | grep -E '\[OK\]|行数=|合计运费|\[FAIL\]'
done

echo ""
echo "=== 产物大小 ==="
ls -lah /tmp/xq_bench | tail -10
```

关键比较指标：
- **Wall time**（`real=Xs`）——用户体感耗时
- **最大 RSS**（`maxmem=...KB`）——避免 16GB 以下机器 OOM
- **输出行数/总行数一致性**——±0.1% 以内算正常（COALESCE 空行过滤差异）
- **SUM(总运费) 一致性**——与基线 `9,392,357.95` 逐位比较，**任何 SQL 改写必须 bit-exact 一致**，否则回滚

---

## 6. 快速兜底：DuckDB 参数不动代码就能 +15%

如果临时急着提速又不想改代码，在 `DuckDBManager::Init()` 后手动加 5 条 SET（在 GUI 的系统设置里做成"性能档位：标准/极速"切换）：

```sql
SET threads = 8;
SET memory_limit = '7GB';
SET preserve_insertion_order = false;
SET enable_object_cache = true;       -- 缓存 read_xlsx 的页
SET temp_directory = '/tmp/duckdb-tmp';  -- 超大数据溢写磁盘（防止 8GB 机器跑 500 万行 OOM）
```

