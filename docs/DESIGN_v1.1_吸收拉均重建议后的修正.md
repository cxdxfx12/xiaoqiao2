# 增量 v1.1：吸收《拉均重建议.md》后的设计修正（行业主流实现对齐）
原设计：`DESIGN_客户级覆写与拉均重合同.md v1.0`（第 1 章到第 9 章）
参考文件：[ref_拉均重建议_市场主流实现.md](file:///Users/cxd/duckdb/xiaoqiao_freight/docs/ref_拉均重建议_市场主流实现.md)
**结论：v1.0 里的"拉均重 = 每件最低均重兜底"不是行业里真正的"拉均重合同"，只是一个"轻小件最低计费重"的小工具。真正的拉均重合同 = 客户+省份分区+账单周期 三维 GROUP 出 AVG(charge_weight)×统一单价，3kg 以下小件走这个通道，3kg 以上继续走阶梯首续重。**
本文档只写 **v1.0 之外要改/要加的东西**，v1.0 里"客户级覆写 3 参数、两 Tab UI、回滚保险、三级链、渐进 TAG 命名"等内容**全部保留**，不重复。

---

## v1.0 → v1.1 核心差异（最重要的 6 处）

| # | 项目 | v1.0（我原先设计的） | v1.1（按《拉均重建议》修正的） | 说明 |
|---|---|---|---|---|
| D1 | "拉均重"的定义 | 单票：`MAX(charge_weight, pieces × min_avg_kg_per_piece)`，每件做兜底 | 批量 GROUP：`AVG(charge_weight)` over (客户 + 省份分区 + 账单周期)，得到一个分区统一单价，3kg 以下每件都按同价收 | 这是最核心的差异，完全两种计费模型 |
| D2 | 表结构（拉均重参数存哪） | `freight_templates` 加 3 列 + `customers` 加 7 列（客户能覆盖模板 min_avg） | **新增两张独立表** `avg_weight_templates`（合同头：阈值/基准价/步长）+ `avg_weight_zones`（合同省分区）；`customers.avg_weight_tpl_id` 外键绑定合同 | 独立表是快递小管家/九数云共同做法，和 surcharge_strategies 表一致，**不再往 freight_templates 塞 3 列拉均重参数**（模板级拉均重=行业罕见，v1.1 直接砍掉） |
| D3 | 和首续重的关系 | 顺序：先 MAX 兜底再走首续重（同一套公式） | 并行分支：`CASE charge_mode` → `avg_weight` 走统一价通道 / `standard` 走 tiered_pricing；**同一批订单里两种模式可以混跑** | 行业通用做法，阈值是 3kg，>3kg 不参与均重池，直接回退阶梯 |
| D4 | 件数 `pieces` | 没有件数就恒 1 件（退化为最低计费重） | 不需要 `pieces`；`AVG()` 天然是票级，分区内每票一条订单，COUNT 直接给样本量 | v1.0 的 `pieces` / `eff_avg_default_pieces` **全部删掉**，少增一列输入 |
| D5 | 样本量保护 / 账单周期 | 没有 | `min_tickets`（默认 50）<样本量直接回退阶梯；账单周期字段 `period` 必传 GROUP 维度 | 主流硬性约束，防小样本失真 + 防月中改合同跨月串账 |
| D6 | 单条 CalcSingle 怎么算 | 正常拉均重公式就能算 | **没法真算**，只能用当前 charge_weight 当模拟 avg_w 估个价，结果框加红字提示"月结会取整月该分区均值" | 这点一定要在 UI 标出来，避免业务员单条报完价，月底账单出来不是同一个价被客户骂 |

---

## 11. 表结构修正（v1.0 里的第 2 章做这 3 个改动，其它不动）

### 11.1 撤销（不做）v1.0 里 freight_templates 的 3 列拉均重
**不做这 3 条 DDL**：
```sql
-- ↓ 删掉这三条，不再往 freight_templates 里塞（行业都是客户→独立合同表，不走模板头）
-- ALTER TABLE freight_templates ADD COLUMN tpl_avg_weight_enabled INTEGER DEFAULT 0;   ← 删
-- ALTER TABLE freight_templates ADD COLUMN tpl_min_avg_kg_per_piece DOUBLE DEFAULT 1.0; ← 删
-- ALTER TABLE freight_templates ADD COLUMN tpl_avg_default_pieces INTEGER DEFAULT 1;   ← 删
```

### 11.2 新增两张独立表（和 surcharge_strategies 同级别）
SQLite + DuckDB DDL 两边都加，和现有规则表一起 `LoadRulesFromSQLite`。
```sql
CREATE TABLE avg_weight_templates (
    avg_tpl_id      VARCHAR(60) PRIMARY KEY,    -- 合同号：'cust_misting_202607'
    template_id     VARCHAR(100) NOT NULL,      -- 关联主运费模板（取分区/阈值外首续重从这里拿）
    name            VARCHAR(200) NOT NULL,      -- 『蜜丝婷-江浙沪均重2.7基准』
    threshold_kg    REAL    DEFAULT 3.0,        -- 参与阈值，> 3.0（左开右闭）走首续重不参与均重
    base_avg_kg     REAL    DEFAULT 0.5,        -- 基准均重 0.5kg
    base_fee        REAL    NOT NULL,           -- ≤ 0.5kg 每票 2.7 元
    step_kg         REAL    DEFAULT 0.1,        -- 超重步长 0.1kg
    step_fee        REAL    DEFAULT 0.2,        -- 每超 0.1kg → +0.2 元/票
    min_tickets     INTEGER DEFAULT 50,         -- 样本量门槛，低于→全组回退阶梯
    period_type     VARCHAR(10) DEFAULT 'month',-- 'month' / 'week' / 'custom' （展示用，GROUP 外部传）
    contract_no     VARCHAR(60) DEFAULT '',     -- 线下合同号（备注）
    is_active       INTEGER DEFAULT 1,
    created_at      TIMESTAMP,
    updated_at      TIMESTAMP
);

CREATE TABLE avg_weight_zones (
    id              INTEGER PRIMARY KEY AUTOINCREMENT,
    avg_tpl_id      VARCHAR(60) NOT NULL,
    zone_code       VARCHAR(20) NOT NULL,       -- 'zone_1'、'zone_2' 可和主模板 zone_groups 重名，但独立维护
    province        VARCHAR(50) NOT NULL,       -- 省名规范：'江苏'（不带省字），和 zone_group_provinces 一致
    UNIQUE(avg_tpl_id, zone_code, province)
);
CREATE INDEX idx_avg_weight_zones_tpl ON avg_weight_zones(avg_tpl_id);
```

### 11.3 customers 表：原来 7 列 → 改为 7+1=8 列（把 3 列拉均重替换为外键 avg_weight_tpl_id）
**原来 v1.0 里的 3 列 → 合并为 1 列外键**：
```sql
-- 客户级覆写（3 参数覆写保留，和 v1.0 一致）+ 拉均重从 4 列缩成 1 列外键
ALTER TABLE customers ADD COLUMN cust_rounding_mode         VARCHAR DEFAULT '';     -- 空=回落模板（保留）
ALTER TABLE customers ADD COLUMN cust_additional_unit       DOUBLE  DEFAULT 0;      -- 0=回落模板（保留）
ALTER TABLE customers ADD COLUMN cust_vol_divisor           INTEGER DEFAULT 0;      -- 0=回落模板（保留）
-- 下面 v1.0 的 4 列拉均重 "cust_avg_enabled / cust_min_avg_kg / cust_avg_default_pieces / cust_contract_no" 全部删掉
--    ↓ 改成 1 列外键 + 1 列纯备注：
ALTER TABLE customers ADD COLUMN avg_weight_tpl_id          VARCHAR(60) NULL;       -- NULL = 不走拉均重合同
ALTER TABLE customers ADD COLUMN cust_contract_no           VARCHAR(60) DEFAULT ''; -- 留着当纯备注（线下合同编号）
```
净增列数：customers 从 v1.0 +7 列 → **v1.1 +5 列**（更省）。

### 11.4 Schema 版本号策略（不变）
`kSchemaVersion` 依旧 10 → 15，v11~14 留回滚空挡位。第 15 版一次性加 2 张独立表 + customers 5 列。

---

## 12. 计费 SQL 设计修正（v1.0 第 4 章改动：原 avg_weight_checked CTE → 换成 4 段 CTE 流水线）
参考文件第 101-137 行 4 段式 CTE。

### 12.1 批量 BuildCalcSQL 最终流水线
顺序：
```
① input_data + active_params（客户覆写 3 参数三级链 → eff_*）
② charge_weight_rounded（用 eff_rounding_mode / eff_vol_divisor 出 charge_weight，跟现在一样）
③ avg_weight_tag          ← 新 CTE：打标签 charge_mode='avg_weight'/'standard'
④ avg_weight_agg          ← 新 CTE：GROUP BY (customer_id, avg_zone, period) 算 AVG / COUNT
⑤ avg_weight_pricing      ← 新 CTE：样本量≥min_tickets 就出 fee_per_ticket，<阈值→NULL=回退阶梯
⑥ matched_zone → matched_tier → base_fee_calc（原来 tiered 首续重路径不动，仅在 charge_mode='standard' 分支走）
⑦ final_fee_merge         ← 新 CTE：charge_mode='avg_weight' AND fee_per_ticket NOT NULL → 用 fee_per_ticket；否则 base_fee
⑧ fuel/remote/strategy surcharges（原样不动，燃料附加费/其它附加费照常乘 base_fee 或 fee_per_ticket）
```

#### 12.1.1 ③ avg_weight_tag CTE 细节
```sql
avg_weight_tag AS (
    SELECT
        cwr.*,
        -- 账单周期：优先用输入列 period（YYYY-MM 或 YYYY-Www）；没有就取 CURRENT_DATE 所在自然月
        COALESCE(NULLIF(cwr.period,''),
                 STRFTIME('%Y-%m', CURRENT_DATE)) AS period,
        CASE
            WHEN c.avg_weight_tpl_id IS NOT NULL
             AND awt.is_active = 1
             AND cwr.charge_weight <= awt.threshold_kg        -- ≤阈值进均重池（=3.0kg 还在池里，左开右闭，边界防扯皮）
             AND az.zone_code IS NOT NULL                     -- 有分区匹配才进，NULL 海外/特殊区自动回退阶梯
                THEN 'avg_weight'
                ELSE 'standard'
        END AS charge_mode,
        az.zone_code AS avg_zone,
        awt.*
    FROM charge_weight_rounded cwr
    LEFT JOIN customers c ON c.customer_id = cwr.customer_id
    LEFT JOIN avg_weight_templates awt ON awt.avg_tpl_id = c.avg_weight_tpl_id AND awt.is_active = 1
    LEFT JOIN avg_weight_zones az
           ON az.avg_tpl_id = awt.avg_tpl_id
          AND REGEXP_REPLACE(az.province, '(省|市|维吾尔自治区|回族自治区|壮族自治区|自治区)$', '')
            = REGEXP_REPLACE(cwr.dest_province, '(省|市|维吾尔自治区|回族自治区|壮族自治区|自治区)$', '')
)
```
**边界处理对齐《建议》第四部分**：
- 3.0kg = 进均重池（左开右闭：charge_mode 条件用 `<=`，阈值外 tier 分支用 `>`）；
- 分区匹配 NULL = standard 分支，绝不参与 AVG；
- 客户合同不激活 (`is_active=0`) = standard。

#### 12.1.2 ④ avg_weight_agg CTE 细节
```sql
avg_weight_agg AS (
    SELECT
        customer_id,
        period,
        avg_zone,
        COUNT(*)                               FILTER (WHERE charge_mode='avg_weight') AS ticket_cnt,
        AVG(charge_weight)                     FILTER (WHERE charge_mode='avg_weight') AS avg_w,
        MAX(threshold_kg)                      FILTER (WHERE charge_mode='avg_weight') AS threshold_kg,
        MAX(base_avg_kg)                       FILTER (WHERE charge_mode='avg_weight') AS base_avg_kg,
        MAX(base_fee)                          FILTER (WHERE charge_mode='avg_weight') AS base_fee,
        MAX(step_kg)                           FILTER (WHERE charge_mode='avg_weight') AS step_kg,
        MAX(step_fee)                          FILTER (WHERE charge_mode='avg_weight') AS step_fee,
        MAX(min_tickets)                       FILTER (WHERE charge_mode='avg_weight') AS min_tickets
    FROM avg_weight_tag
    GROUP BY customer_id, period, avg_zone
)
```
- 同客户不同合同月份 → 分开 GROUP，避免改合同后把上月数据一起算（《建议》P70）；
- 样本量 `ticket_cnt < min_tickets` → 这组回退阶梯（《建议》P65）。

#### 12.1.3 ⑤ avg_weight_pricing CTE 细节
```sql
avg_weight_pricing AS (
    SELECT
        a.*,
        CASE
            WHEN a.ticket_cnt IS NULL OR a.ticket_cnt < COALESCE(a.min_tickets,50)
                THEN NULL
            ELSE
                a.base_fee + GREATEST(0,
                    CEIL( (a.avg_w - a.base_avg_kg)
                          / GREATEST(a.step_kg, 0.0001) )
                    * a.step_fee
                )
        END AS fee_per_ticket
    FROM avg_weight_agg a
)
```
典型配置数值：base_fee=2.7, base_avg_kg=0.5, step_kg=0.1, step_fee=0.2 → `avg_w=0.62kg` → `CEIL((0.62-0.5)/0.1)*0.2 = 0.2` → 每票 2.9 元（完全匹配《建议》P42 的例子）。

#### 12.1.4 ⑦ final_fee_merge CTE（把均重统一价 vs 阶梯首续重 两个分支最后合起来）
```sql
final_fee_merge AS (
    SELECT
        rac.*,
        awp.avg_w,
        awp.ticket_cnt,
        awp.min_tickets,
        awp.fee_per_ticket,
        CASE
            WHEN rac.charge_mode = 'avg_weight' AND awp.fee_per_ticket IS NOT NULL
                THEN awp.fee_per_ticket
            ELSE bfc.base_fee
        END AS base_fee,
        CASE WHEN rac.charge_mode = 'avg_weight' AND awp.fee_per_ticket IS NOT NULL
             THEN 'avg_weight' ELSE bfc.charge_mode END AS final_charge_mode
    FROM avg_weight_tag rac
    LEFT JOIN base_fee_calc bfc USING (order_id, customer_id)
    LEFT JOIN avg_weight_pricing awp
           ON awp.customer_id = rac.customer_id
          AND awp.period      = rac.period
          AND awp.avg_zone    = rac.avg_zone
)
```
后面的燃油/策略附加费计算都从 `final_fee_merge.base_fee` 读，两条路径无需复制逻辑。

### 12.2 单条 CalcSingle 路径：**Mock 预览 + 强提示**
行业做不到单条真·算均重（均重需要一堆样本）。单条结果框里这么写：
```
【预估运费：¥2.90】
  本客户「蜜丝婷」在合同期内走「拉均重模式」，
  单条按当前订单重量 0.3kg 模拟估算，
  实际月结会与同月份"江浙沪皖"同分区其他订单取均值。
  3.0kg 以下统一价 = 基准价¥2.7 + 每超0.1kg×¥0.2
  3.0kg 以上按中通标准模板阶梯首续重。
```
实现：把 `charge_weight` 当 `avg_w`，直接跑 12.1.3 的公式，不做 GROUP，不查样本量 `min_tickets`（单条一定 <50，但用户既然点了单发就给他估个数）。

### 12.3 逐票回写列（导出 Excel 时必须带上，《建议》P57 标为核心能力）
在 `_ut_output` / 导出表最后**追加 5 列**，客户对账用（不能删，删了客户没法跟快递小管家对）：
```
charge_mode | avg_w(分区均重kg) | ticket_cnt(分区样本量) | min_tickets(门槛) | avg_zone(匹配分区号)
standard    | NULL              | NULL                   | NULL              | NULL
avg_weight  | 0.62              | 1247                   | 50                | zone_1
```
这 5 列是《建议》P57/P143 说的"快递小管家活下来的核心能力"——逐票给 avg_w、分区号、适用单价，让客户拿 Excel 就能核对，不用下钻 APP。

---

## 13. UI 设计修正（v1.0 第 6 章改 2 处）
编辑客户对话框两 Tab 不变；**Tab2「高级计费设置」里拉均重部分重画**：

```
─── Tab 2 高级计费设置 ─────────────────────────────────
  ☑ 客户专属进位模式       [▼ 国标 0.1kg 进一     ∨]   （v1.0 保留，不动）
  ☑ 客户专属续重单位(kg)   [____]                      （v1.0 保留，不动）
  ☑ 客户专属体积重除数     [____]                      （v1.0 保留，不动）
 ───────── 拉均重合同（大客专属月结）──────────        ← 这整块全部重画
  □ 启用拉均重合同（本客户月结专用）
      合 同 名 称：[蜜丝婷-江浙沪均重2.7基准   ]
      绑定模板（分区/阶梯取这里）：[▼ zto_standard ∨]   ← 3kg以上走哪个模板的首续重
      均 重 合 同： [▼ 新建/选择已有合同…       ∨]   ← 下拉 avg_weight_templates，没选点"新建"弹向导弹窗（6 字段：阈值/基准均重/基准价/步长/步价/样本门槛）
      阈值 3.0kg 以下走均重，以上走阶梯首续重      ← 只读说明，提醒边界
      样本量 < 50 单时本分区自动回退阶梯
 ─────────────────────────────────────────────
```
**为什么在客户对话框里"新建均重合同"向导弹窗而不是独立菜单项？**
因为《建议》P59/P167 明确写：拉均重合同 = 大客专属，100 个客户里只有 5~10 个会签，绑到客户上最合理；真要复用，给已有均重合同加个"复制为新合同"按钮就够了，不单独占系统菜单位置。

---

## 14. 测试用例修正（v1.0 第 7 章 → v1.1 改 18 断言）
新增 v1.1 专属 3 大类共 24 断言（原来的 18 断言中"拉均重按件兜底"的 6 条删掉，换成下面的）：

### 14.1 用例 8 — 真·拉均重，批量 200 件合成 1 个均价
构造：客户 D 绑均重合同 = 阈值 3kg / 基准均重 0.5kg=¥2.7 / 步 0.1kg=¥0.2 / 样本量 50；江浙沪皖一区。
输入 150 条订单（50×0.3kg、50×0.8kg、50×3.0kg、0×3.001kg）。
- 期望：
  - 前 150 条 → charge_mode=`avg_weight`；
  - AVG = (0.3×50+0.8×50+3.0×50)/150 = 1.3667kg；
  - 每票运费 = 2.7 + CEIL((1.3667-0.5)/0.1) × 0.2 = 2.7 + 9×0.2 = ¥4.5（跟单条 0.3kg / 0.8kg / 3.0kg 本身的重量没关系，**每票同价**）。
- 再加 1 条 3.001kg → charge_mode=`standard`，按原 tiered_pricing 3~30kg 首续重算出单独价格（证明两模式共存且边界左开右闭正确）。
- 再做"只给 20 条小样本" → 样本量 20 < 50 → 全部回退阶梯（证明 min_tickets 生效）。

共 12 条断言。

### 14.2 用例 9 — 单条 Mock 预览
客户 D 单发 0.3kg → 估 2.7 元，结果框里必须含"**单条模拟估算，月结取整月分区均值**"字样（UI 测字符串）。

### 14.3 用例 10 — 导出 Excel 回写 5 列
运行 CalcBatch 后 DESCRIBE `_ut_output` → 确认 5 列名存在；`charge_mode='avg_weight'` 的行 avg_w / ticket_cnt / min_tickets 三列数值非 NULL 且正确。

---

## 15. 回滚保险 v1.1 增补
在 R3 回滚 SQL `docs/sql/rollback_customer_override_v15.sql` 末尾追加：
```sql
-- -------- v1.1 新增：两张独立拉均重合同表 --------
-- 温和降级：仅置 is_active=0 不删数据
UPDATE avg_weight_templates SET is_active = 0 WHERE 1=1;
UPDATE customers SET avg_weight_tpl_id = NULL WHERE 1=1;

-- 物理删除（废弃本功能）：
-- BEGIN;
-- DROP TABLE IF EXISTS avg_weight_zones;
-- DROP TABLE IF EXISTS avg_weight_templates;
-- COMMIT;
```
另外 R4 开关补充：`feature_avg_weight_contract`（和客户覆写 3 参数的开关独立，3 参数开关关了拉均重合同依旧能跑，因为合同是独立定价模型不依赖那三个客户覆写字段），两个双保险。

---

## 16. 渐进 TAG 不变（第 8 章仍按 Step2~7 执行，只是 Step2 DB Schema 换成 v1.1 的 DDL）
TAG 命名照旧：`co-step2-db-schema → co-step3-duckdb → co-step4-repo-doublewrite → co-step5-calcservice → co-step6-ui → co-step7-test-finish`，每步回滚可独立。
