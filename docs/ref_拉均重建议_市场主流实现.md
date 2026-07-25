还有拉均重主流软件是怎么设计的
拉均重（平均重量计费）——主流软件的设计模式
「拉均重」是快递网点与电商大客户（日发 3000 票+）签的月结合同专用计费模式，核心思路：同一省份分区（如华东一区）里，先把客户 3kg 内所有小件的平均重量算出来，再按这个平均重量×统一单价=每票运费，而不是按单个包裹实际重量算。好处是快递网点吃到超重件的溢价，客户拿到整体更低的均价，双赢。
目前市场上能做拉均重的系统主要是 3 类：① 快递小管家/快宝 类网点级 SaaS、② 九数云/帆软 BI 类对账工具、③ 菜鸟/顺丰企业后台的大客合同结算。设计上统一遵循「参数 → 分组 → 算均重 → 费用 → 超重件单独走首续重」5 步，下面是完整的模块设计。

一、核心计费逻辑（快递小管家 + 九数云联合案例）
沿海浙江/江苏/上海/安徽最通用的拉均重合同：
参数项	典型配置	说明
参与拉均重的重量阈值	≤ 3.0 kg（小件通道）	3kg 以上走首续重，不参与（避免大件把均重拉爆）
省份分组（分区）	一区 = 江浙沪皖	同分区所有省份合并算一次均重
基准均重 / 起步费	≤ 0.5 kg → 每票 2.7 元	均重不超过 0.5kg 收 2.7/票
超重加价规则	每超出 0.1kg → 加收 0.2 元	0.6kg 收 2.7+0.2=2.9；0.8kg=3.1，连续线性加价
分组维度	客户 + 省份分区 + 账单周期（月/周）	每个客户/每个区单独拉平均
二、数据处理流水线（5 步，主流 SaaS 都这么切）
Plain Text


┌──────────────────────────┐
│  ①  计费模型参数配置     │   (模板级，系统设置 → 拉均重合同)
│  阈值 / 分区表 / 基准价 /│
│  超重步长 / 步长价格     │
└──────────┬───────────────┘
           ▼
┌──────────────────────────┐
│  ②  打标签 + 分组        │
│  a) <阈值 → 均重池        │   每票加字段 charge_mode=avg_weight
│  b) ≥阈值 → 首续重池      │   charge_mode=standard_tiered
│  c) 根据收件省打分区Tag   │   zone_tag=一区/二区/三区
└──────────┬───────────────┘
           ▼
┌──────────────────────────┐
│  ③  聚合算均重            │   GROUP BY (customer_id, zone_tag)
│  AVG(charge_weight)       │
│  COUNT(*) 件数            │
│  得 客户-分区 均重表      │   江浙沪皖 avg_w=0.62kg
└──────────┬───────────────┘
           ▼
┌──────────────────────────┐
│  ④  均重池算费            │
│  fee_per_ticket = base_price +
│    ceil((avg_w - base_w)/step) * step_price
│  3kg 内每票都这个价      │   0.62kg → base 2.7 + 1×0.2 = 2.9 元/票
│  分区内所有件数 × 均重单价 │
└──────────┬───────────────┘
           ▼
┌──────────────────────────┐
│  ⑤  超重件单独算 + 汇总  │   首续重 = first_price + N × additional_price
│  均重池费 + 超重件费       │   = 最终客户该分区总运费
└──────────────────────────┘

三、主流软件在各环节的设计对比
功能模块	快宝 / 快递小管家（网点端）	九数云/帆软 BI（财务端对账）	菜鸟商家工作台·大客版
① 参数配置 UI	✅ 独立「拉均重合同」向导弹窗，下拉：阈值、分区表、基准价、超重步长价四项必填	✅ 看板参数卡片，用户自定义字段列	✅ 合同模块，绑定大客编号 + 网点授权
省份分区	✅ 复用运费模板里的 zone_groups（一组多省），直接下拉选	✅ 省份表手动 JOIN 分组	✅ 按快递公司官方分区（华东一区/二区…）
参与门槛（阈值）	✅ 常用 3kg，可自定义 1/2/3kg	✅ 参数自定义滑块	✅ 按合同录入，不可随意改
超重加价步长	✅ 固定 0.1kg 步长 + 0.01~9.99 元自定义	✅ 表达式自由定义	✅ 合同模板，支持 0.1kg / 0.5kg 步长
账单维度	✅ 票级（每条都回填均重单价），下载后一条订单一行，含 avg_w、zone_tag、fee_item	✅ 汇总表 + 下钻，点击均重数值能看到哪些单拉高了	✅ 总部账单，按分区分客户汇总，明细要到 APP 里查
和首续重共存	✅ 阈值下均重，阈值上首续重，同一票订单走一个分支	✅ 过滤条件分别算，最终 UNION ALL	✅ 同左，两种计费模式在合同中并存
多客户隔离	✅ 客户级绑定"拉均重模板ID"，不同客户不同阈值/步长/分区	✅ 需要手动分表	✅ 合同编号隔离，天然隔离
二次调整（客户砍价）	✅ 计算完可手动 override「均重价」按钮，重算全部	✅ 改参数重跑	✅ 合同变更换版本号，历史不回滚
和系统现有阶梯价关系	是并行的第 2 套定价模型，比 tiered_pricing 优先级高（命中拉均重合同就不走阶梯）	外部核算，不影响内部主模型	合同优先级最高
四、关键边界处理（最容易踩坑）
快递小管家和九数云都对这些做了硬性约束，小乔要跟上：
边界场景	主流处理	设计理由
某客户某分区只有 < N 件（如 50 件以下）	❌ 不参与拉均重，回退首续重	样本太少均价失真，快递网点不会签这种情况
混合客户（均重客 + 阶梯客）同批次结算	CASE WHEN customer.avg_weight_tpl_id>0 THEN 均重模式 ELSE 阶梯模式 END	客户表加一个外键 avg_weight_tpl_id，默认 NULL
某件 3.001kg（边界值）	charge_weight > 3.0 才进首续重	左开右闭，3.0 整 kg 还在均重池，防网点和客户扯皮
分区省份匹配 NULL（海外/特殊地区）	自动回退首续重，不参与任何均重分组	防均重 AVG 被极端值污染
同一个分区里既有 0.3kg 小货也有 2.9kg 大货	保留原样混合，不做额外分层	这就是客户要"拉均重"的初衷：小件补贴大件，平均下来比阶梯价更便宜
账单周期结算	必须按自然周/月 GROUP 窗口，不能跨周期拉平均，均重是按合同周期算的	比如客户这个月改了合同，月尾单独封账
五、给小乔系统落地的推荐设计
当前小乔核心定价模型是 tiered_pricing（6 档阶梯）+ surcharge_strategies 策略，建议把拉均重做顶层的"前置分支"：
1. Schema 新增（参考快递小管家合同模型）
Plain Text


avg_weight_templates (
  avg_tpl_id      PK  VARCHAR(60)
  template_id         VARCHAR(100)   -- 关联哪个运费模板（省分区、首续重从这里拿）
  name                VARCHAR(200)   -- 『蜜丝婷-江浙沪均重2.7』
  threshold_kg        REAL  DEFAULT 3.0   -- 参与阈值，>阈值走首续重
  base_avg_kg         REAL  DEFAULT 0.5   -- 基准均重
  base_fee            REAL  NOT NULL      -- ≤基准均重 每票 2.7元
  step_kg             REAL  DEFAULT 0.1   -- 超重步长
  step_fee            REAL  DEFAULT 0.2   -- 每步加价
  min_tickets         INTEGER DEFAULT 50  -- 低于该件数 → 回退阶梯
  is_active           INTEGER DEFAULT 1
)

avg_weight_zones (   -- 均重模板下 哪些省 算 哪个分区（可复用 zone_groups，但更灵活）
  id          PK
  avg_tpl_id      FK
  zone_code       VARCHAR(20)   -- 'zone_1'
  province        VARCHAR(50)   -- '上海'
  UNIQUE(avg_tpl_id, zone_code, province)
)

customers 表加列：
  avg_weight_tpl_id VARCHAR(60) NULL -- 绑定均重模板，NULL 不走
2. CalcSQL 前置打标签 CTE（批量结算最需要，单条也能按 mock 均重=charge_weight 估费）
在 _input_normalized 之后加：
SQL


avg_weight_tag AS (
  SELECT
    rac.*,
    CASE WHEN c.avg_weight_tpl_id IS NOT NULL AND rac.charge_weight <= awt.threshold_kg
         THEN 'avg_weight' ELSE 'standard' END AS charge_mode,
    az.zone_code AS avg_zone,
    awt.*
  FROM _input_normalized rac
  LEFT JOIN customers c ON rac.customer_id = c.customer_id
  LEFT JOIN avg_weight_templates awt ON c.avg_weight_tpl_id = awt.avg_tpl_id AND awt.is_active=1
  LEFT JOIN avg_weight_zones az
         ON az.avg_tpl_id = awt.avg_tpl_id
        AND REGEXP_REPLACE(az.province, '(省|市|…自治区)$', '')
          = REGEXP_REPLACE(rac.dest_province, 同正则, '')
),
avg_weight_agg AS (
  SELECT customer_id, avg_zone,
         AVG(CASE WHEN charge_mode='avg_weight' THEN charge_weight END) AS avg_w,
         COUNT(*) FILTER (WHERE charge_mode='avg_weight') AS ticket_cnt
  FROM avg_weight_tag
  GROUP BY customer_id, avg_zone
),
avg_weight_pricing AS (
  SELECT
    a.*,
    CASE WHEN a.ticket_cnt < awt.min_tickets THEN NULL
         ELSE awt.base_fee
            + GREATEST(0, CEIL((a.avg_w - awt.base_avg_kg)/awt.step_kg) * awt.step_fee)
    END AS fee_per_ticket
  FROM avg_weight_agg a
  LEFT JOIN avg_weight_templates awt ON a.avg_zone IS NOT NULL
)
最后汇总：charge_mode='avg_weight' AND fee_per_ticket NOT NULL → 用 fee_per_ticket；否则走 tiered_pricing。
. 单条计算对话框（Mock 预览）
单条没法真算均重，用当前输入重量模拟当成 avg_w，预估值并在结果里提示"* 为均重模式下按 1 件模拟；月结时会与该月同分区其他件合并取均值"，让客户有预期。

六、一句话总结市场主流实现
均重模式 ≠ 对单个包裹算平均重量，而是『客户 + 省份分区 + 账单周期』三维 GROUP 出的 AVG(charge_weight) 乘以统一单价，3kg 以下小件走这个通道，其余件继续按阶梯首续重算。
实现难点不在算法，而在：① 参数（阈值/步长/基准价）UI 配置够不够像快递小管家那么傻瓜化；② 分区匹配准不准；③ 票级账单能不能把 avg_w、分区号、适用单价这三列都逐票写回到 Excel，方便客户核对。这些是市场主流（快宝/快递小管家）能活下来的核心能力。





续重单位和续重进位规则和体积重以及拉均重是不是用客户专属规则更好，不用全局，因为不是所有客户都需要用这样的模式，大部分客户还是正常模式
昨天 10:11





结论先行：模板层级为主 + 客户级可覆写（开关可选），绝对是最优设计
完全认同你的判断。这四项配置不应该是全局的，原因有三：
1. 80% 长尾客户用默认值：普通散客/小客根本不知道这些细节，自动继承「中通标准」模板的默认 1kg / 进一法 / 6000 系数 / 不走拉均重就好。
2. 20% 头部客户差异化：像蜜丝婷（月发 10 万票）、珀莱雅这种确实会签 0.1kg 进一、体积 8000、甚至单独的拉均重合同，逐客绑定或在"客户专属报价模板"里改才合理。
3. 合同性质：续重进位规则是合同条款（邮政局要求"合同内公示"），不是系统偏好。和 tiered_pricing 同级放在模板里，签合同的时候一起配置，出纠纷时能直接追溯。

推荐层级设计（优先级：客户覆写 > 模板默认 > 代码兜底）
配置项	主存储位置（80% 场景）	可选覆写位置（20% 大客）	NULL 兜底
续重单位 additional_unit	freight_templates.tpl_additional_unit（模板级）	customers.cust_additional_unit（客户级）	1.0 kg
续重进位规则 rounding_mode	freight_templates.tpl_rounding_mode（模板级）	customers.cust_rounding_mode（客户级）	'ceil_0_1kg'（0.1kg 进一，国标推荐）
体积重除数 vol_weight_divisor	freight_templates.tpl_vol_divisor（模板级）	customers.cust_vol_divisor（客户级）	6000
拉均重合同 avg_tpl	直接挂 customers.avg_weight_tpl_id（客户级）	—— 不需要模板级	NULL = 不走均重
核心原则：80% 的客户在 customers 表这四个覆写列全是 NULL，直接 JOIN freight_templates 读模板默认值；只有 20% 的头部大客会在客户编辑页把「使用模板默认」关掉，手动填自己的专属配置。

1. 四张表的 Schema 追加（最小改动）
SQL


-- ========== freight_templates 新增（模板默认） ==========
ALTER TABLE freight_templates ADD COLUMN tpl_additional_unit   REAL DEFAULT 1.0;     -- 续重单位 1/0.5/0.1kg
ALTER TABLE freight_templates ADD COLUMN tpl_rounding_mode     VARCHAR(30) DEFAULT 'ceil_0_1kg';
  -- 允许取值：ceil_1kg / ceil_0_5kg / ceil_0_1kg / round_0_1kg / floor_no_round
ALTER TABLE freight_templates ADD COLUMN tpl_vol_divisor       INTEGER DEFAULT 6000;  -- 体积重除数

-- ========== customers 新增（客户级覆写，NULL = 使用模板默认） ==========
ALTER TABLE customers ADD COLUMN cust_additional_unit   REAL;
ALTER TABLE customers ADD COLUMN cust_rounding_mode     VARCHAR(30);
ALTER TABLE customers ADD COLUMN cust_vol_divisor       INTEGER;
ALTER TABLE customers ADD COLUMN avg_weight_tpl_id      VARCHAR(60) NULL;  -- 拉均重合同绑定

-- ========== 拉均重合同独立表（纯客户级，像 surcharge_customers） ==========
-- 同之前设计，不重复。avg_weight_templates 主键 avg_tpl_id 被 customers.avg_weight_tpl_id 引用
Schema 版本号从目前 13 → 14，sqlite_rule_repository::Init() 加一段迁移，老数据全部用 DEFAULT（即 1.0 / ceil_0_1kg / 6000 / NULL），对老用户零感知。
2. 计算 SQL 如何读（CalcSingle & BuildCalcSQL 通用 CTE）
在任何 tiered_pricing / 体积重之前，先构造一行「本客户本模板的最终计费参数」：
SQL


-- 新增最开头 CTE：active_params（只有 1 行，客户级优先，模板级兜底，最后代码默认）
active_params AS (
  SELECT
    -- 续重单位
    COALESCE(c.cust_additional_unit, ft.tpl_additional_unit, 1.0) AS final_additional_unit,
    -- 续重进位
    COALESCE(c.cust_rounding_mode, ft.tpl_rounding_mode, 'ceil_0_1kg') AS final_rounding_mode,
    -- 体积重除数
    COALESCE(c.cust_vol_divisor, ft.tpl_vol_divisor, 6000) AS final_vol_divisor,
    -- 是否走拉均重（customer_id 级 NULL 判断）
    c.avg_weight_tpl_id
  FROM freight_templates ft
  LEFT JOIN customers c ON c.customer_id = '%5'   -- 单条：输入的客户ID；批量：rac.customer_id
  WHERE ft.template_id = '%1'
),
-- 体积重 & 计费重（原来这里硬编码 6000，改成读 final_vol_divisor）
charge_weight_calc AS (
  SELECT
    GREATEST(actual_weight,
      CASE WHEN vol_length > 0 AND vol_width > 0 AND vol_height > 0
           THEN vol_length*vol_width*vol_height / final_vol_divisor
           ELSE NULLIF(vol_weight, 0) END
    ) AS raw_charge_weight,
    final_additional_unit, final_rounding_mode, final_vol_divisor, avg_weight_tpl_id
  FROM active_params, input_data LIMIT 1
),
-- 续重进位（原来没有这步，直接乘，现在标准化）
final_charge AS (
  SELECT
    CASE final_rounding_mode
      WHEN 'ceil_1kg'     THEN CEIL(raw_charge_weight)
      WHEN 'ceil_0_5kg'   THEN CEIL(raw_charge_weight * 2) / 2
      WHEN 'ceil_0_1kg'   THEN CEIL(raw_charge_weight * 10) / 10
      WHEN 'round_0_1kg'  THEN ROUND(raw_charge_weight * 10) / 10
      WHEN 'floor_no_round' THEN raw_charge_weight
      ELSE CEIL(raw_charge_weight * 10) / 10
    END AS charge_weight,
    final_additional_unit, avg_weight_tpl_id
  FROM charge_weight_calc
)
-- 后续 tiered_pricing 命中后算续重 N：
-- N = CEIL( (charge_weight - first_weight) / final_additional_unit )
-- 运费 = first_price + N * additional_price

3. UI 改造点（两个对话框就能搞定，不要系统设置里出现全局开关）
① 模板编辑对话框 template_edit_dialog.cpp
顶部新增**「计费参数」卡片**（在模板名称、vol_weight_ratio 下方），因为参数是模板级默认，所以模板编辑时配置一次，所有绑定该模板的客户自动继承：
Plain Text


┌─────────────────────────────────────────────────────────────┐
│ 计费参数（绑定此模板的客户将默认继承，客户可单独覆写）       │
│  续重进位：[▼ 0.1kg 进一（国标推荐） v] 1kg进一/0.5kg进一/… │
│  续重单位：[▼ 1.0 kg v]  或自定义[  0.100 ] kg              │
│  体积重除数：[▼ 6000（普通快递）v] 自定义[       ]          │
│                        (顺丰=6000 / 大件物流=4800 / 快运=8000)│
└─────────────────────────────────────────────────────────────┘
（原来的 vol_weight_ratio 文本框保留但隐藏，自动和 tpl_vol_divisor 双向同步，兼容历史）
② 客户编辑对话框 customer_edit_dialog.cpp
新增**「高级计费设置」Tab 或折叠面板**，默认展开"使用模板默认"，只有大客才动：
Plain Text


 ┌☑ 使用模板【中通标准】默认参数                                ┐
 │                                                             │
 │ ☐ 覆写为客户专属（以下仅在取消勾选模板默认时生效）           │
 │    续重进位：[▼ 0.1kg 进一 v]                               │
 │    续重单位：[▼ 1.0 kg v]                                   │
 │    体积重除数：[▼ 6000 v]                                   │
 │                                                             │
 │ ┌ 拉均重合同（本客户专属月结专用）─────────────────────────┐ │
 │ │ ☑ 启用拉均重                                            │ │
 │ │   合同名称：[蜜丝婷-江浙沪2.7基准价]                     │ │
 │ │   绑定模板：[▼ 新建/选择均重合同… v]                    │ │
 │ │   阈值：3.0 kg 内走均重，以上走首续重阶梯                │ │
 │ └─────────────────────────────────────────────────────────┘ │
 └─────────────────────────────────────────────────────────────┘

4. 各模式优先级总览（一句话）
配置读取 = 客户级覆写 → 模板级默认 → 代码 fallback 默认值 计费模式 = 客户绑定 avg_weight_tpl_id 非 NULL 且 charge_weight ≤ 3kg → 拉均重，否则 → 阶梯首续重

5. 为什么绝对比全局开关好（实际案例）
场景	全局开关做法（差）	模板+客户覆写（优）
同一用户同时接了「蜜丝婷」和 20 个散户	要么全 0.1kg（散户首重被进位得多收），要么全 1kg（蜜丝婷合同不匹配，对账扯皮）✅ 无解	蜜丝婷客户勾选覆写→0.1kg 进一；散户全 NULL 继承中通标准 1kg ✅ 互不干扰
蜜丝婷体积重签的 8000，珀莱雅签的 6000	全局只能设一个，总有一家错	两个客户各自覆写 cust_vol_divisor = 8000 / 6000，或分别建两个客户专属报价模板各改 tpl_vol_divisor ✅
散户 1 万单 + 大客 1 万单同批结算	模式统一，要么散户吃亏要么大客吃亏	active_params CTE 按行 JOIN，每条订单按客户/模板动态取参数，同一批次混合计算 ✅
客户合同到期改进位规则	系统全局一改，所有历史账单重算全变，对账灾难	只改该客户的 cust_rounding_mode，历史其他客户零影响 ✅
新员工配置失误改错全局	全部客户计算都错	模板级默认改错了，影响的也只是绑定这个模板的一批客户（通常只有一个），风险可控 ✅
下一步落地建议
如果现在要实现，我建议分 2 个 commit 独立推进（和主分支解耦）：
Commit 1：续重单位 + 进位规则 + 体积重（三项模板/客户级化）
* Schema 13→14：freight_templates + customers 加 6 列，默认值写死
* CTE 重写：active_params 前置，体积重 & charge_weight 都从 final_* 读
* 模板编辑 dialog 计费参数卡片 + 客户编辑 dialog 覆写开关
* 现有 6 档 tiered_pricing 的 additional_unit 字段从"每档独立"改成"模板头为默认，档级可单独覆写=NULL 继承默认"，不破坏老数据
Commit 2：拉均重合同（独立大模块，晚一步不影响主流程）
* 新增两张表 avg_weight_templates / avg_weight_zones
* customers 加 avg_weight_tpl_id
* 批量 CalcSQL 加 4 个 CTE（打标签→聚合→算均重单价→阈值外回退阶梯）
* 客户编辑 dialog 加「启用拉均重 & 绑定合同」面板
* 单条对话框用"本单重量模拟均重" + 提示文案，不算真均重
