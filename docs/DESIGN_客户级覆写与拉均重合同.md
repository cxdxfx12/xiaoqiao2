# 功能设计：客户级计费参数覆写 + 拉均重合同（大客专属）
设计文档版本 v1.0 —— 状态：**设计完成/待开发**
基线提交：`c2681a4` | 基线回滚 Tag：`baseline-before-customer-override`
编写时间：2026-07-26

---

## 0. 设计目标与非目标

### 0.1 目标
1. **客户级参数覆写**：每个大客户能单独覆盖"续重进位模式 / 续重单位 / 体积重除数"3 个计费参数，不再受报价模板强制统一；
   - 不勾客户专属 = 回落模板配置 = **完全兼容老客户老订单已有价格**
2. **拉均重合同（大客专属）**：客户签了"每件最低平均 X kg"合同后，一票多件的轻小件不再按实重 0.3kg 收费，而是按 `件数 × min_avg_kg_per_piece` 和现有计费重取 MAX 兜底；
3. **UI 入口不新增菜单项**：复用 `customer_setting_dialog.cpp` 左侧列表 → 「编辑客户」按钮打开的对话框；把现有 400×300 四行表单改成 **600×460 两 Tab（基础信息 / 高级计费设置）**；
4. **零新增 Excel 输入列**：不给 `pieces` 也能用，自动回退到"每件 1 件"（退化为单票最低计费重兜底）。

### 0.2 非目标（先不做）
- ❌ 全局级 / 分区级 参数再叠加；只做客户级 + 模板级 两级链；
- ❌ 复杂合同有效期、多版本合同；合同编号 `cust_contract_no` 只做备注字段展示用；
- ❌ 不新增独立"客户合同管理"菜单项；
- ❌ 模板级拉均重 UI（`freight_templates` 表先建好 3 列 + 默认值，供客户级"回退模板"用，但 UI 不暴露——**这是昨天你说"拉均重先不弄"的折中方案**）。

---

## 1. 回滚保险清单（必须保留到上线后第 30 天）

| # | 保险类型 | 内容 | 触发方式 |
|---|---|---|---|
| R1 | **Git TAG** | `baseline-before-customer-override` （commit `c2681a4`） | `git reset --hard baseline-before-customer-override` |
| R2 | **设计文档** | 本文件，附 R1/R3/R4 三保险具体执行步骤 | 本文件 |
| R3 | **SQLite 回滚脚本** | `docs/sql/rollback_customer_override_v15.sql` —— 把 customers/freight_templates 新列设为 DEFAULT 值 + 可选 DROP COLUMN（SQLite 3.35+ 支持） | `sqlite3 rules.db < docs/sql/rollback_customer_override_v15.sql` |
| R4 | **运行时降级开关** | `AppConfig::Instance().feature_customer_override`（默认 true；出问题加 `-DNO_CUST_OVERRIDE` 编译或改 `feature_customer_override=false`，三级链自动跳过，100% 回到今天前的行为） | 见章节 5.3 |
| R5 | **每步一个小 TAG** | 每完成下面章节 2-6 中一大步，打一个递进 TAG：`co-step2-db-schema`、`co-step3-duckdb`、`co-step4-repo-doublewrite`、`co-step5-calcservice`、`co-step6-ui`、`co-step7-test`。共 6 个 TAG。 | `git tag co-stepX-xxx` 后再开发下一步 |

### R1 执行步骤（最粗暴回滚，全丢）
```bash
cd /Users/cxd/duckdb/xiaoqiao_freight
git reset --hard baseline-before-customer-override
# 重新编译：
cd build && rm -rf CMakeCache.txt CMakeFiles src/ui tools ; make -j8
```

### R3 执行步骤（只回滚 DB 表结构+数据，不丢代码）
先跑 `docs/sql/rollback_customer_override_v15.sql`：再把 AppConfig 里 `feature_customer_override=false`，重新编译。等价于 R4+R3 联合降级。

---

## 2. 数据库 Schema 设计（第 2 步开发用）

### 2.1 SQLite：kSchemaVersion 10 → 15（一次性升 5，留 11~14 给回滚位）
在文件 [sqlite_rule_repository.cpp](file:///Users/cxd/duckdb/xiaoqiao_freight/src/db/sqlite_rule_repository.cpp) 的 `SqliteRuleRepository::Init()` 中：

```cpp
static constexpr int kSchemaVersion = 15;   // 原 10，跳 5 步；回滚时降到 10 就跑回滚 SQL
```

#### 2.1.1 `freight_templates` 新增 3 列（模板级拉均重，先存值但 UI 不开）
```sql
-- CREATE TABLE freight_templates 末尾追加（老库用 ALTER TABLE，升级脚本中 for (int v=11..15) 各加一列）
ALTER TABLE freight_templates ADD COLUMN tpl_avg_weight_enabled INTEGER DEFAULT 0;   -- 0关 1开
ALTER TABLE freight_templates ADD COLUMN tpl_min_avg_kg_per_piece DOUBLE DEFAULT 1.0;
ALTER TABLE freight_templates ADD COLUMN tpl_avg_default_pieces INTEGER DEFAULT 1;
```
> 为什么 UI 不开还要加？因为客户级覆写有"回落模板"三级链（2.4），模板列不建会让 CASE WHEN 报找不到列。默认 0 相当于**模板关**，老数据零影响。

#### 2.1.2 `customers` 新增 7 列（客户级覆写 + 拉均重合同）
```sql
ALTER TABLE customers ADD COLUMN cust_rounding_mode         VARCHAR DEFAULT '';    -- 空=回落模板
ALTER TABLE customers ADD COLUMN cust_additional_unit       DOUBLE  DEFAULT 0;     -- 0=回落模板
ALTER TABLE customers ADD COLUMN cust_vol_divisor           INTEGER DEFAULT 0;     -- 0=回落模板
ALTER TABLE customers ADD COLUMN cust_avg_enabled           INTEGER DEFAULT 0;     -- -1强制关 0随模板 1强制开
ALTER TABLE customers ADD COLUMN cust_min_avg_kg_per_piece  DOUBLE  DEFAULT 0;     -- 0=回落模板
ALTER TABLE customers ADD COLUMN cust_avg_default_pieces    INTEGER DEFAULT 0;     -- 0=回落模板
ALTER TABLE customers ADD COLUMN cust_contract_no           VARCHAR DEFAULT '';    -- 纯备注
```

#### 2.1.3 SQLite 升级顺序（避免老库漏列）
```
for version in 11→12→13→14→15：
  11: ALTER freight_templates ADD tpl_avg_weight_enabled
  12: ALTER freight_templates ADD tpl_min_avg_kg_per_piece
  13: ALTER freight_templates ADD tpl_avg_default_pieces
  14: 空（留回滚位）
  15: ALTER customers 一次加 7 列（老 SQLite 一次 ALTER 七列是 7 条语句，每条包事务）
最后 user_version=15
```

### 2.2 DuckDB：两处 DDL 改
文件 [duckdb_manager.cpp](file:///Users/cxd/duckdb/xiaoqiao_freight/src/db/duckdb_manager.cpp)：

#### 2.2.1 `freight_templates`（章节 2.1.1 三列补进去）
在已修过的 12 列 DDL（`L105-L122`）里再加 3 列 → 扩到 15 列。

#### 2.2.2 `customers`（章节 2.1.2 七列补进去）
把 `customers` DDL 从 `contact_person, contact_phone, address, created_at, updated_at` 后追加 7 列。

#### 2.2.3 `LoadRulesFromSQLite` 列映射显式化（避免 `SELECT *` 顺序飘）
把 `INSERT INTO ... SELECT * FROM sqlite_attach` 改成显式列名列表（`customers` 11 列、`freight_templates` 15 列），两边列名顺序完全一致。这一步是 **R3 回滚后 DuckDB 不报错**的关键。

---

## 3. 仓储层双写（第 3 步开发用）

文件 [sqlite_rule_repository.cpp](file:///Users/cxd/duckdb/xiaoqiao_freight/src/db/sqlite_rule_repository.cpp)：

| 函数 | 要做 | 行号落点（近似） |
|---|---|---|
| `AddTemplate(QVariantMap &tpl)` 现在的 `L625` INSERT | 11 列扩成 14 列（3 个 tpl_avg_* 列塞进去，取 map 默认 `tpl_avg_weight_enabled=0 / 1.0 / 1`） | `L639-L671` 两个 prepare 分支都扩 |
| `CreateDefaultTemplate()` 默认 zto_standard | 同样 INSERT 14 列，`tpl_avg_*=0/1.0/1`（=默认关，保证老客户升级后 zto_standard 价格和今天一致） | `CreateDefaultTemplate` 里的 INSERT |
| `AddCustomer(QVariantMap &c)` | INSERT 8 列扩 15 列；没传的字段都按 DEFAULT 空/0 塞（=回落模板） | `AddCustomer()` 里的 INSERT |
| `UpdateCustomer(QString id, QVariantMap &c)` | UPDATE 语句增加 7 个新列的 SET 子句；每个新列"只在 map 有 key 时改"（避免把没动的客户覆写清空）——这是客户对话框"勾了才保存"的仓储层协议 | `UpdateCustomer()` 里的 UPDATE |
| `Customer::toMap() / fromMap()` | 7 个 cust_* 字段 + 3 个 template tpl_avg_* 字段在 QVariantMap 里进进出出 | `freight_types.hpp` 里 `struct Customer` 扩展 7 个成员 |

---

## 4. 计费服务：客户级 → 模板级 → 全局默认 三级链（第 4 步开发）

文件 [calc_service.cpp](file:///Users/cxd/duckdb/xiaoqiao_freight/src/services/calc_service.cpp)：

### 4.1 BuildCalcSQL 批量路径（`BuildCalcSQL(L369-611)`）

#### 4.1.1 `input_data` CTE 可选 pieces
```diff
   COALESCE(weight, 0) AS weight,
   COALESCE(vol_weight, 0) AS vol_weight,
   COALESCE(customer_id, '') AS customer_id,
+  COALESCE(pieces, 0)   AS pieces,    -- 输入表没有 pieces 列就全 0
```
没有 pieces 列怎么办？→ 在 `input_data` 之前用 `GetTableColumns()` 探测一下，没 pieces 就写 `0 AS pieces`。

#### 4.1.2 `template_info` CTE 里抽出 6 个 eff_* 列（三级链精华）
把现有 `L398-L401` 四条 COALESCE 替换成 6 条 CASE WHEN：
```sql
-- ① 进位模式
CASE
    WHEN feature_customer_override AND c.cust_rounding_mode IS NOT NULL AND c.cust_rounding_mode <> ''
    THEN c.cust_rounding_mode
    ELSE COALESCE(NULLIF(ft.tpl_rounding_mode, ''), 'ceil_0_1kg')
END AS eff_rounding_mode,

-- ② 续重单位 kg
CASE
    WHEN feature_customer_override AND c.cust_additional_unit > 0
    THEN c.cust_additional_unit
    ELSE COALESCE(NULLIF(ft.tpl_additional_unit, 0), NULLIF(ft.additional_unit, 0), 1.0)
END AS eff_additional_unit,

-- ③ 体积重除数
CASE
    WHEN feature_customer_override AND c.cust_vol_divisor > 0
    THEN c.cust_vol_divisor
    ELSE COALESCE(NULLIF(ft.tpl_vol_divisor, 0),
                  CAST(NULLIF(ft.vol_weight_ratio, 0) AS INTEGER), 6000)
END AS eff_vol_divisor,

-- ④ 拉均重开关（三态）
CASE
    WHEN NOT feature_customer_override               THEN 0
    WHEN c.cust_avg_enabled = 1                      THEN 1
    WHEN c.cust_avg_enabled = -1                     THEN 0
    ELSE COALESCE(NULLIF(ft.tpl_avg_weight_enabled,0), 0)
END AS eff_avg_enabled,

-- ⑤ 每件最低均重 kg
CASE
    WHEN feature_customer_override AND c.cust_min_avg_kg_per_piece > 0
    THEN c.cust_min_avg_kg_per_piece
    ELSE COALESCE(NULLIF(ft.tpl_min_avg_kg_per_piece, 0), 1.0)
END AS eff_min_avg_kg,

-- ⑥ 缺件数默认几件
CASE
    WHEN feature_customer_override AND c.cust_avg_default_pieces > 0
    THEN c.cust_avg_default_pieces
    ELSE COALESCE(NULLIF(ft.tpl_avg_default_pieces, 0), 1)
END AS eff_avg_default_pieces
```
> `feature_customer_override` 是编译期+运行时双保险（章节 5.3）。关掉后所有客户级分支不进，等价于今天前的行为。

#### 4.1.3 `input_data` 里体积重的计算（先把 tpl_vol_divisor → eff_vol_divisor）
当前 `input_data` 里的 `raw_charge_weight` 是硬编码 `MAX(vol_weight, weight)` → 其实如果 `vol_weight` 列是"体积 L×W×H"而不是"已经除过的体积重"，我们需要先除 `eff_vol_divisor`。现在的设计里 **vol_weight 表示输入已是体积重**，所以除数已经在外部应用了。三级链在 `vol_weight = 0` 的场景（输入是长×宽×高，让引擎自己算体积重）——**今天先不做**，用 `eff_vol_divisor` 只在"输入给了长宽高列"分支用，当前默认分支保持不变。

#### 4.1.4 拉均重：在 `raw_charge_weight` 后、进位前插 `avg_weight_checked CTE`
```sql
avg_weight_checked AS (
    SELECT
        ti.*,
        CASE WHEN ti.eff_avg_enabled = 1
             THEN GREATEST(ti.raw_charge_weight,
                           COALESCE(NULLIF(ti.pieces,0), ti.eff_avg_default_pieces, 1)
                           * ti.eff_min_avg_kg)
             ELSE ti.raw_charge_weight
        END AS raw_charge_weight2
    FROM template_info ti
)
```
后面的 `charge_weight_rounded / matched_zone / matched_tier` 引用列一律从 `ti.raw_charge_weight` → `awc.raw_charge_weight2`。

### 4.2 CalcSingle 单发路径（`CalcSingle()` 内的一段 SQL）
和 4.1 完全镜像：`eff_*` 6 列 + `avg_weight_checked` 都要加；否则批量和单发会算出不一致。4.1 和 4.2 完成后一定要过 billing_params_test 用例 3（章节 7.2）。

---

## 5. 运行时降级开关（R4 保险）

### 5.1 编译期宏
CMakeLists.txt 里加：
```cmake
option(ENABLE_CUSTOMER_OVERRIDE "Enable customer-level billing override + avg-weight contract" ON)
if(NOT ENABLE_CUSTOMER_OVERRIDE)
    add_compile_definitions(NO_CUST_OVERRIDE)
endif()
```
编译命令加 `-DENABLE_CUSTOMER_OVERRIDE=OFF` 就把整个功能去掉，等价于今天前的代码（R4 编译期版本）。

### 5.2 运行时开关
[app_config.hpp + app_config.cpp](file:///Users/cxd/duckdb/xiaoqiao_freight/src/core/app_config.cpp) 加：
```cpp
bool AppConfig::feature_customer_override() const {
#ifdef NO_CUST_OVERRIDE
    return false;
#else
    return m_feature_customer_override; // 默认 true，可通过 config.ini "feature.customer_override=0" 关
#endif
}
```

### 5.3 开关关掉时的保证
`eff_rounding_mode` 等 6 条 CASE WHEN 第一个分支都带 `AND feature_customer_override` → 关掉就**绝对不读** customers 的 7 个新列，老 DB 没这些列时也照样跑通（=R3 回滚不彻底时仍能降级）。

---

## 6. UI：编辑客户对话框 → 两 Tab（第 5 步开发）

入口：`customer_setting_dialog.cpp` → 左侧 QListWidget 选中客户 → 「编辑客户」按钮 `btnEditCustomer_` clicked → 打开 `EditCustomerDialog`。

### 6.1 布局：600 × 460，QTabWidget 两页
**Tab 1「基础信息」**（原四行扩到 7 行）：
```
┌──────────────────────────────────────────┐
│ 客户名称        [____________________]  * │ 必填
│ 客户编号        [____________________]    │ 保存后只读
│ 折扣率(%)       [____]                    │ 默认 1.00，0.9 = 九折
│ 默认计费模板    [▼ zto_standard      ∨]   │ 下拉 freight_templates
│ 联系人          [____________________]    │
│ 联系电话        [____________________]    │
│ 地址            [____________________]    │
│ 合同编号(备注)  [____________________]    │ cust_contract_no，纯展示
└──────────────────────────────────────────┘
```

**Tab 2「高级计费设置」（客户级覆写 + 拉均重合同大客专属）**
> 每条左侧一个 **☑ 启用客户专属** checkbox（勾上才激活右侧控件；不勾 = 保存 NULL/0 = 回落模板 = 保持今天前的价格）
```
┌──────────────────────────────────────────────────────┐
│  □ 客户专属进位模式     [▼ 国标 0.1kg 进一      ∨]   │  5选1
│  □ 客户专属续重单位(kg) [____]                        │  1.0 / 0.5 / 0.1
│  □ 客户专属体积重除数   [____]                        │  6000/5000/8000/12000
│ ───────── 拉均重合同（大客专属） ─────────             │
│  拉均重合同： {○ 按模板 ● 强制开 ○ 强制关 }          │  cust_avg_enabled 三态
│  □ 每件最低平均计费重(kg)   [1.00]                    │
│  □ 输入缺件数时按几件算     [1]                       │
│                                                      │
│  说明：勾上上面任意项即覆盖该客户对应模板默认；        │
│        不勾的项完全跟随模板。                         │
└──────────────────────────────────────────────────────┘
```

### 6.2 保存协议（UI ↔ 仓储层）
UI 构造 QVariantMap：
```cpp
QVariantMap cust;
if (chkRounding->isChecked())  cust["cust_rounding_mode"] = cmbRounding->currentData();
else /* 不勾，key 不带入 map */;
repo.UpdateCustomer(id, cust); // 仓储层只 UPDATE map 里有 key 的列（章节 3）
```
**不带 key = 不动数据库原值 = 不勾不丢。**

---

## 7. 测试用例设计（第 6 步开发）

文件 [billing_params_test/main.cpp](file:///Users/cxd/duckdb/xiaoqiao_freight/tools/billing_params_test/main.cpp)。
现有 6 大类 30 断言 **必须先全绿**（昨天已经完成），在尾部再追加 3 大类共 18 断言：

### 7.1 用例 7 — 客户级 3 参数覆盖模板（10 断言）
构造：模板A = `ceil_0.1kg` + 续重 1kg + 除数 6000；客户A 勾：
- `cust_rounding_mode = ceil_1kg`
- `cust_additional_unit = 0.5`
- `cust_vol_divisor = 5000`

输入：江苏 1.23kg 实重；客户 A。
期望：计重 = CEIL(1.23)=2kg（客户级 ceil_1kg，**≠ 模板 1.3kg**），续重单价按 0.5kg 出；
另：40×30×20=24000÷5000=4.8 体重要→客户级 ceil_1kg=5（**≠ 模板 4kg**）。

共 10 条断言：charge_weight / base_fee × 4 单 + 批量 2 单对齐。

### 7.2 用例 8 — 客户强制开拉均重（6 断言）
构造：模板A `tpl_avg_weight_enabled=0`；客户 A `cust_avg_enabled=1 + cust_min_avg_kg_per_piece=1.0`。
- 输入 1：江苏 0.3kg 实重 1 件 → 期望计费重 `MAX(0.3, 1×1.0)=1.0`（轻小件兜底）
- 输入 2：江苏 3kg 实重、缺 pieces → 按 `cust_avg_default_pieces=5` → 期望计费重 `MAX(3, 5×1)=5`
- 客户 B `cust_avg_enabled=-1`（强制关），模板B `tpl_avg=1开min=1.0` → 0.3kg 还是 0.3（客户级强制关优先级最高）

### 7.3 用例 9 — 不勾客户专属 = 回落模板（2 断言）
客户 C 一项也不勾；订单和用例 7 相同 → 期望结果和"用模板默认值直接算"**完全一致**（证明"不勾 = 零影响"）。

---

## 8. 渐进开发步骤（每天打 TAG，方便逐段回滚）
| 步骤 | 做啥 | 打完 TAG 名 | 回滚到此的 git 命令 |
|---|---|---|---|
| Step 1 | 本文档 + git 基线 TAG + 回滚脚本 | `baseline-before-customer-override`（已打 commit `c2681a4`） | `git reset --hard baseline-before-customer-override` |
| Step 2 | SQLite DDL（v10→v15）+ 回滚 SQL | `co-step2-db-schema` | `git reset --hard co-step2-db-schema` |
| Step 3 | DuckDB DDL + LoadRules 显式列 | `co-step3-duckdb` | ↑ |
| Step 4 | 仓储层双写 + struct Customer 扩字段 | `co-step4-repo-doublewrite` | ↑ |
| Step 5 | CalcService 三级链 + avg CTE + 单发/批量一致 | `co-step5-calcservice` | ↑ |
| Step 6 | UI 两 Tab + 客户勾选项 ↔ 仓储 UpdateCustomer | `co-step6-ui` | ↑ |
| Step 7 | 新增 3 测试用例 + 全量 billing_params_test 绿 | `co-step7-test-finish` | ↑ |

---

## 9. 上线前后检查清单
| 阶段 | 检查 | 通过标准 |
|---|---|---|
| 开发中 Step 4 后 | 打开 App → 新建客户 → 直接保存（不勾任何客户专属项）→ CalcSingle 模板测试 6 项全绿和今天一模一样 | 30 断言 100% 相等 |
| 开发中 Step 6 后 | 老客户数据 100 条随机抽 10 条，用 feature_customer_override=true/false 各算一次→false 价格=今天前；true 只有在勾了客户专属的订单才变 | 10/10 对比过 |
| 上线前一天 | 关掉 `feature_customer_override`，生产数据跑一次，跟昨日报表对总运费差 | 差值 < 0.01 元（浮点误差） |
| 上线当天 | 先跑 feature_customer_override=false 一小时；确认没问题再切 true | 同上 |
| 出事故 1 分钟内 | 改 config.ini 或重编 R4→回 R3→最后 git R1 | 总运费和事故前完全一致 |
