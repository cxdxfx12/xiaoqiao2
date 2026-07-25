# v1.2 最终设计：轻小件 0.xkg 拉均重 + 一口价/阶梯 边界决策树
设计版本 v1.2 — 状态：设计完成/待开发
前置版本：
 - v1.0 [DESIGN_客户级覆写与拉均重合同.md](file:///Users/cxd/duckdb/xiaoqiao_freight/docs/DESIGN_客户级覆写与拉均重合同.md)
 - v1.1 [DESIGN_v1.1_吸收拉均重建议后的修正.md](file:///Users/cxd/duckdb/xiaoqiao_freight/docs/DESIGN_v1.1_吸收拉均重建议后的修正.md)
参考归档：[ref_拉均重建议_市场主流实现.md](file:///Users/cxd/duckdb/xiaoqiao_freight/docs/ref_拉均重建议_市场主流实现.md)
基线 TAG：`baseline-before-customer-override`（commit `c2681a4`）
上一步 TAG：`co-step1b-merge-拉均重建议-finish`（commit `5d20ee0`）

---

## 本版本已落定的用户决策（默认 A + P1 + S1，用户确认写入）

| 决策问题 | 最终选择 | 说明 |
|---|---|---|
| 0.xkg 轻小件区间 | **A. 独立双门槛（pool_min ~ pool_max）+ 均重加价封顶** | 合同级 4 个新字段：`avg_pool_min_kg` / `avg_pool_max_kg` / `base_avg_kg` / `avg_fee_cap_kg` |
| 拉均重 vs 一口价优先级 | **P1. 合同最高** → 档内一口价 → `default_no_weight_fee` | 撞签合同就按合同，没合同没命中档内一口价才用兜底 |
| 超 0.xkg 区间订单 | **S1. 无缝回退 tiered_pricing**（档内一口价 0–3kg 还是 2.26/2.46/3.56/4.76；3–30kg 首续重） | 完全兼容老算法，业务员零适应成本 |
| 行业新增两层上限 | ✅ **订单级 pool 门槛 + 均重级 fee 封顶** | 正是本次用户提的"1kg 可设定上限" |

---

## 1. 4 层重量维度（本项目 0.xkg 拉均重区别于行业 3kg 通用版的核心创新）
一张表把所有"kg上限/下限"概念区分清楚，以后开发/业务/财务都看这张不扯皮：

| 层级 | 字段名 | 位置 | 默认值 | 典型配置例子（用户本次要的） | 含义 |
|---|---|---|---|---|---|
| L1 | **weight** | 输入表（单票订单实重/体积重进位后） | — | 0.2kg / 0.5kg / 0.8kg / 1.2kg / 4kg | 每个订单本身的 charge_weight（先按模板进位好） |
| L2 订单级池门槛（"单个订单能不能进均重池"） | `avg_pool_min_kg` ←**新增**<br>`avg_pool_max_kg` ←**新增** | `avg_weight_templates` 合同表 | 0.0 kg<br>**1.0 kg** ✨（用户要求的"拉均重上限1kg可设"=这个） | 0.0 kg<br>1.0 kg | 单票 charge_weight 在 [0.0, 1.0] **闭区间** 才能进池；<br>1.0001kg 及以上 → **不进池** → 直接走 S1 回退原阶梯（档内一口价/首续重） |
| L3 均重本身（整批 AVG 算出来） | `avg_w`（CTE 中间值） | `avg_weight_agg` 临时结果 | — | 0.3kg（刚到约定值）<br>0.62kg（超了一些）<br>1.4kg（超了封顶线） | 对所有进了池的订单 GROUP BY(客户+分区+账单月) 算出来的平均 kg |
| L4 均重加价封顶（"就算均重再高最多按1kg收"） | `avg_fee_cap_kg` ←**新增** | `avg_weight_templates` 合同表 | **1.0 kg** ✨（用户要求的"拉均重上限1kg可设"=这个） | 1.0 kg | 计算 step_kg × step_fee 加价时，对 avg_w 取 MIN(avg_w, avg_fee_cap_kg) 后再套公式，避免无限加；<br>或者超了后直接回退阶梯（见 over_cap_mode） |
| L5 约定起算基准（"合同里说的拉均重=0.3kg"=这个） | `base_avg_kg` ←**保留并重命名语义**<br>`base_fee` ← 保留 | `avg_weight_templates` 合同表 | **0.3 kg** ✨（用户要的"约定拉均重0.3kg"=这个）<br>2.7 元 | 0.3 kg → ¥2.7 | avg_w ≤ base_avg_kg → 一律收 base_fee 基准价；<br>超过 base_avg_kg 开始走 step_kg/step_fee 每0.1kg加价 |

> **两层"1kg上限"的本质区别**（最容易搞混的地方，业务沟通时一定要说清楚）：
> - `avg_pool_max_kg = 1kg` → **单个订单进池资格判定**（你1.2kg的订单连进池子都不让进，直接回阶梯）
> - `avg_fee_cap_kg = 1kg` → **进了池就算出均重，给客户的封顶加价**（整批均值到 1.4kg 也最多按 1kg 收，不会再往上加）
> 两个都做成合同字段，各自独立可改，默认都是 1kg。

---

## 2. 合同表 DDL 最终版（avg_weight_templates 12 字段 → 18 字段）
在 v1.1 基础上新增 **5 个字段**（2 个池门槛 + 1 个封顶 + 1 个超封顶策略 + 1 个 base_avg_kg 默认值调整），total 18 列：

```sql
CREATE TABLE avg_weight_templates (
    avg_tpl_id      VARCHAR(60) PRIMARY KEY,
    template_id     VARCHAR(100) NOT NULL,    -- 绑定主模板（用于超池/超封顶回退阶梯 + 取省分区可选复用开关）
    name            VARCHAR(200) NOT NULL,    -- 『珀莱雅-江浙沪0.3基准1kg封顶』

    -- ↓↓↓ v1.2 新增 5 列（轻小件专属：0.xkg 约定 + 两层 1kg 上限）↓↓↓
    avg_pool_min_kg REAL    DEFAULT 0.0,      -- L2 池门槛下沿（0.0kg以上才能进池；默认0=不设下限；也可以设成 0.05kg 过滤掉无重件）
    avg_pool_max_kg REAL    DEFAULT 1.0,      -- L2 池门槛上沿（用户要的"1kg可设上限"①）；闭区间：≤1.0kg才有资格进池
    base_avg_kg     REAL    DEFAULT 0.3,      -- L5 约定起算均重（用户说"约定拉均重0.3kg"=这个），≤0.3一律基准价
    avg_fee_cap_kg  REAL    DEFAULT 1.0,      -- L4 均重加价封顶kg（用户要的"1kg可设上限"②）；超了按 over_cap_mode 处理
    over_cap_mode   INTEGER DEFAULT 0,        -- L4 超封顶策略：0=按avg_fee_cap_kg封顶算价不再加  1=超了就整体回退阶梯  2=单票逐个超的回退阶梯（本项目先做0+1，2留以后）
    -- ↑↑↑ v1.2 新增结束 ↑↑↑

    threshold_kg    REAL    DEFAULT 1.0,      -- v1.1 里的 threshold 不再=3kg，默认等于 avg_pool_max_kg（兼容保留；真正的池边界用上面两个字段，threshold只读做镜像显示）
    base_fee        REAL    NOT NULL,         -- L5 ≤base_avg_kg时的基准价，例：2.7元
    step_kg         REAL    DEFAULT 0.1,      -- L5 超base_avg_kg后，每多少kg加价一档（用户要的"每超0.1kg加…"=这个）
    step_fee        REAL    DEFAULT 0.2,      -- L5 每一步加多少钱（例：0.2元/步）
    min_tickets     INTEGER DEFAULT 50,       -- 样本量门槛；<50单就整组回退阶梯（行业标准）

    -- 其它配套字段
    reuse_zone_groups INTEGER DEFAULT 1,      -- 省分区来源：1=直接复用主模板template_id下的 zone_groups（省写一次avg_weight_zones）  0=用独立 avg_weight_zones 表（可自定义不同分区）
    period_type     VARCHAR(10) DEFAULT 'month',   -- month/week/custom（显示用，GROUP传period列）
    contract_no     VARCHAR(60) DEFAULT '',   -- 线下合同编号
    is_active       INTEGER DEFAULT 1,
    created_at      TIMESTAMP,
    updated_at      TIMESTAMP
);
-- 保留独立分区表（reuse_zone_groups=0 时才用；默认不用，复用主模板省分区，业务少配置一次）
CREATE TABLE avg_weight_zones (
    id              INTEGER PRIMARY KEY AUTOINCREMENT,
    avg_tpl_id      VARCHAR(60) NOT NULL,
    zone_code       VARCHAR(20) NOT NULL,
    province        VARCHAR(50) NOT NULL,
    UNIQUE(avg_tpl_id, zone_code, province)
);
CREATE INDEX idx_avg_weight_zones_tpl ON avg_weight_zones(avg_tpl_id);
```

### 为什么加 `reuse_zone_groups` 默认 1（强烈推荐默认开）
用户要的典型配置："江浙沪皖拉均重 0.3kg，超0.1加 0.x，1kg 上限"——而江浙沪皖分区本来就在主运费模板的 zone_groups（华东一区）里配好了。加这个开关直接复用主模板分区，不用在均重合同里再选一遍省，少一步配置，少一次选错省的扯皮。

---

## 3. 最终决策树（每票订单一行，自上而下撞第一个命中就走哪个）
把 P1（合同优先级最高）+ A（0.xkg 独立双门槛）+ S1（回退阶梯）+ L2/L4 两层上限 合成 7 步决策：
```
（每票订单已经经过：客户覆写3参数 eff_* → 模板进位 → charge_weight 算好）

STEP 1  无重量面单？ weight=0/空 → 收 default_no_weight_fee（老规则兜底，永远最后）
          ↓ 否
STEP 2  客户有没有绑定有效 avg_weight_tpl_id（is_active=1）？
        ├─ 否 → 直接走原 tiered_pricing（档内一口价/首续重，S1）✅ 结束
        └─ 是 → STEP 3
STEP 3  省分区能匹配吗？（根据 reuse_zone_groups=1 读主模板 zone_groups 或 avg_weight_zones）
        ├─ 匹配 NULL（特殊区/海外）→ 回退 tiered_pricing（S1，边界约束P69）✅ 结束
        └─ 匹配到 → STEP 4（该订单获得一个临时 zone_tag）
STEP 4  L2 池门槛：本票 charge_weight ∈ [avg_pool_min_kg, avg_pool_max_kg] 闭区间？
        ├─ 否（例如 1.001kg、或 -0.1kg 脏数据）→ 本票标记 charge_mode='standard'，不进池 → STEP 7 走原阶梯（S1）✅
        └─ 是（例如 0.3kg / 0.8kg / 1.0kg，都在池里）→ 本票标记 charge_mode='avg_weight'，进入 avg_weight_tag CTE，参与 GROUP → STEP 5
STEP 5  GROUP BY (客户+账单月period+zone_tag) 算：
        ticket_cnt = COUNT(charge_mode=avg_weight)
        avg_w      = AVG(charge_weight)  （仅对进了池的票取平均）
        → 样本量判断：ticket_cnt < min_tickets（默认50）→ 整组一起回退 tiered_pricing（行业边界P65）→ STEP 7 ✅
        → 够样本 → STEP 6
STEP 6  计算该组的 fee_per_ticket（L3/L5/L4 公式）：
        effective_avg_w = MIN(avg_w, avg_fee_cap_kg)    -- L4 封顶 kg
        over_kg         = GREATEST(0, effective_avg_w - base_avg_kg)
        steps           = CEIL(over_kg / step_kg)        -- 超 0.1kg 就是一步
        fee_per_ticket  = base_fee + steps * step_fee

        超封顶策略（over_cap_mode）：
          0 按封顶价继续算：上面 effective_avg_w=MIN 已自然处理，avg_w=1.4→按1.0算；继续 STEP 7a ✅
          1 超了就整组回退阶梯：IF avg_w > avg_fee_cap_kg THEN 整组回退 STEP 7b tiered_pricing

STEP 7 最终写回 per-ticket 行的 base_fee / 导出名：
  7a charge_mode='avg_weight' 且 fee_per_ticket 有效 → base_fee = fee_per_ticket（**合同赢档内一口价，P1**）
  7b 否则 → 用原 tiered_pricing 算法（档内一口价 0-3kg = 2.26/2.46/3.56/4.76，3kg+ 首续重，S1）
```

### ⭐ 3 个用户典型数值例子（按你要的参数：pool=0~1kg, base_avg=0.3kg, base_fee=¥2.7, step=0.1kg=¥0.2, fee_cap=1.0kg, over_cap_mode=0）

#### 例 1：整批均值刚到约定值（最理想情况）
- 池内 200 单 charge_weight = 0.2~0.3kg，AVG = 0.29kg
- 结果：`effective_avg_w = MIN(0.29, 1.0) = 0.29 ≤ base_avg_kg 0.3` → **每票统一 ¥2.7**
- 对比老档内一口价：这 200 单老算法是 0.5kg 档一口价 ¥2.26，合同价贵 0.44/单（**业务场景合理：客户要拉均重，多给几毛换网点整体打包利润**，P1 合同赢了）

#### 例 2：整批均值超约定但在封顶内（最常发生）
- 池内 500 单 → avg_w = 0.62kg
- 结果：`effective_avg_w=MIN(0.62,1.0)=0.62; over_kg=0.32; steps=CEIL(0.32/0.1)=4`
- 每票 = ¥2.7 + 4 × ¥0.2 = **¥3.5 / 票**（原老 0.5kg 档 2.26/2.46 都没用，合同赢 P1）

#### 例 3（边界 1：单票超池上限）
- 来一个 1.001kg 的订单 → STEP 4 区间判定 `1.001 > avg_pool_max_kg 1.0` → **不进池** → 回 tiered_pricing 档内一口价 tier_1_2 → **¥3.56**（老算法完全生效，S1）
- 来一个 1.8kg 的订单 → 同样超池 1kg → 回 tier_1_2 → ¥3.56 一口价

#### 例 4（边界 2：整批均值超封顶 kg，over_cap_mode=0 封顶）
- 池内 200 单 avg_w = 1.4kg（均值已经超 1kg 了）
- 结果：`effective_avg_w=MIN(1.4,1.0)=1.0; over_kg=0.7; steps=7` → 每票 = 2.7 + 7×0.2 = **¥4.1 封顶**（不会再按 1.4kg × 11 步算到 4.9）
- （如果 over_cap_mode=1）→ 整组 200 单全部回退 tiered_pricing：0.2kg→2.26 / 0.8kg→2.46 / 1.0kg→3.56，各回各档

#### 例 5（边界 3：样本量不够）
- 新客户首月只发 10 单 → ticket_cnt = 10 < min_tickets 50 → **整组 10 单全部回退 tiered_pricing**（不按均重，防网点吃亏，行业共识）

---

## 4. 与原 tiered_pricing 一口价的并行关系（P1 合同优先级最高的含义，最容易扯皮的地方）
用 0~3kg 重量段把两条路径的结果并排，客户/业务员一眼能看懂：

| charge_weight | 原 tiered_pricing 档内一口价（additional_price=0） | 合同命中后（avg_pool=0~1，base=0.3，cap=1） | 最终 base_fee（P1 规则） |
|---|---|---|---|
| 0 kg 或空 | default_no_weight_fee（例如 ¥3） | — | ¥3（STEP 1 兜底） |
| 0.2kg（≤1kg进池） | tier_0_0.5 一口价 ¥2.26 | 进池，月底按整批 AVG 出统一价（例1=2.7，例2=3.5…） | **合同价**（P1 合同赢） |
| 0.5kg（≤1kg进池） | tier_0_0.5 一口价 ¥2.26 | 进池（同0.2kg） | 合同价 |
| 1.0kg（=pool_max，闭区间→进池） | tier_0.5_1 一口价 ¥2.46 | 进池 | 合同价 |
| **1.001kg**（>pool_max，池外） | tier_1_2 一口价 ¥3.56 | **不进池** → STEP 4 出去回阶梯 | **¥3.56（原档内一口价，S1）** |
| 2.8kg（>1kg，池外） | tier_2_3 一口价 ¥4.76 | 不进池 → 回阶梯 | ¥4.76 |
| 3.5kg（>3kg，池外） | tier_3_30 首续重：¥3.76 + 0.5kg×¥0.8 = ¥4.16 | 不进池 → 回阶梯 | ¥4.16（首续重） |

> **P1 合同优先级的边界**：**只有进了池（L2 区间命中）+ 样本量够 + 没触发 over_cap_mode=1**，合同价才会覆盖档内一口价；**池外（>avg_pool_max_kg）的票 100% 回老算法**。所以业务员签合同时要讲清楚："1kg以下的江浙沪皖才走拉均重，1kg以上还按老报价表，不会乱。"

---

## 5. UI 改（客户 Tab2 拉均重部分细化成 8 字段 + 说明块）
对应 18 字段合同，在客户编辑对话框 Tab2「启用拉均重合同」勾上后，展开如下表单（**默认值直接写用户要的：pool 0~1kg，base_avg=0.3，step=0.1/0.2，cap=1kg，min_tickets=50，over_cap_mode=按封顶**）：
```
☑ 启用拉均重合同（本客户月结专用，0.xkg 轻小件）
   ┌ 基础配置 ─────────────────────────────────────────────────┐
   │ 合同名称：      [珀莱雅-江浙沪0.3基准1kg封顶     ]        │
   │ 绑定主模板：    [▼ zto_standard              ∨]           │  ← 回退阶梯 从这拿价，省分区可直接复用
   │ 线下合同号：    [XS2026-07-001                 ]         │
   └─────────────────────────────────────────────────────────┘
   ┌ 拉均重量化参数（默认值已按0.3kg/1kg填好，可改）──────────┐
   │ ☑ 复用主模板省分区（省配置，推荐）                         │  ← reuse_zone_groups=1（不勾才让你选独立分区）
   │ 进池订单重量范围： [0.00]  ~  [1.00]  kg                 │  ← avg_pool_min/max（L2 两层门槛）
   │ 约定基准均重：    [0.30] kg → 基准价 [2.70] 元/票       │  ← base_avg_kg + base_fee
   │ 超基准加价：每   [0.10] kg →  +  [0.20] 元              │  ← step_kg + step_fee
   │ 均重加价封顶：    [1.00] kg                                │  ← avg_fee_cap_kg（L4 上限，第二个1kg）
   │   超封顶策略：  (●) 按1kg封顶算价  (○) 整组回退阶梯       │  ← over_cap_mode
   │ 样本量门槛：    [50] 单/月（低于则整组回退阶梯）          │  ← min_tickets
   └─────────────────────────────────────────────────────────┘
   说明：
   ① 单个订单 ≤1kg 才进拉均重池，>1kg 的订单按主报价表一口价/阶梯首续重；
   ② 月底取"本客户+江浙沪皖分区+当月"所有进池订单算平均重量；
   ③ 平均重量 ≤0.3kg → 统一 2.7元/票；超 0.3kg 每 0.1kg 加 0.2元；最多加到 1kg 封顶；
   ④ 低于 50 单的月份不做均重，按老报价表算（避免样本失真）。
```

---

## 6. 表结构 + 回滚脚本配套改动
### 6.1 v1.2 回滚保险
`docs/sql/rollback_customer_override_v15.sql` 再加一段：
```sql
-- -------- v1.2 新增：5 个轻小件字段 + 样本量（温和降级：重置默认；物理删除留注释）
-- 温和降级：字段置默认值 不丢结构，未来可重新启用
UPDATE avg_weight_templates SET
    avg_pool_min_kg = 0.0,
    avg_pool_max_kg = 1.0,
    base_avg_kg     = 0.3,
    avg_fee_cap_kg  = 1.0,
    over_cap_mode   = 0,
    step_kg         = 0.1,
    step_fee        = 0.2,
    min_tickets     = 50
WHERE 1=1;
-- 物理删除：
-- ALTER TABLE avg_weight_templates DROP COLUMN avg_pool_min_kg;
-- ALTER TABLE avg_weight_templates DROP COLUMN avg_pool_max_kg;
-- ALTER TABLE avg_weight_templates DROP COLUMN base_avg_kg;
-- ALTER TABLE avg_weight_templates DROP COLUMN avg_fee_cap_kg;
-- ALTER TABLE avg_weight_templates DROP COLUMN over_cap_mode;
```

### 6.2 Schema 版本号保持 10→15（v1.1/v1.2 都在 v15 版一起落，不需要再升 v16，因为还没真正写进过生产库，避免空版本号）
如果以后真有用户用了 v1.1 的老 12 字段合同，再单独升级 v16 补 5 列；现在一次性全部落 v15。

---

## 7. 测试用例（billing_params_test 新增 1 大组 = 8 断言）
在 v1.1 的用例 8/9/10 基础上，**加一组用例 11：双层 1kg 上限 A/P1/S1 边界组合**：
| 用例 | 条件 | 期望 |
|---|---|---|
| 11-1 | charge_weight=1.001kg, pool_max=1.0 | charge_mode=standard → 回 tier_1_2 一口价 3.56（边界开右闭验证） |
| 11-2 | charge_weight=1.0, pool_max=1.0 | charge_mode=avg_weight → 进池（闭区间≤验证） |
| 11-3 | avg_w=0.3 → base_fee 2.7 | ≤base_avg_kg → 基准价（无step加价） |
| 11-4 | avg_w=0.62 fee_cap=1.0 | fee = 2.7+4×0.2=3.5（4步，非封顶路径） |
| 11-5 | avg_w=1.4 fee_cap=1.0 over_cap_mode=0(封顶) | fee = 2.7+7×0.2=4.1（按1kg封顶，不再超加） |
| 11-6 | avg_w=1.4 over_cap_mode=1(回退阶梯) → 该组 charge_mode 变回 standard，各回档内一口价（0.2→2.26 / 0.8→2.46） | 2 条断言 |
| 11-7 | ticket_cnt=10 < min_tickets=50 → 整组回退阶梯 | 基准确认：20单内0.2kg还是原2.26，不按合同2.7 |

---

## 8. 渐进开发 TAG（不变，仍 7 步，只是 Step2 DB Schema 已经是 v1.2 的 18 列合同表）
TAG 顺序：`co-step2-db-schema` → `co-step3-duckdb` → `co-step4-repo-doublewrite` → `co-step5-calcservice` → `co-step6-ui` → `co-step7-test-finish`，每步 TAG 完再下一步，随时可单步回滚。
