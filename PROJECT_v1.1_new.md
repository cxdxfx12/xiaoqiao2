# 小乔运费结算系统 - 项目文档

> **🚨 最新改动 2026-07-27 v1.09 — 拉均重真实场景端到端打通！**
> 🔥 核心修复 2 项：① 订单客户编号填中文名称时匹配不到客户；② 方案A进池资格多一层 template_id 多余过滤导致不命中。
> 其他升级：CMake 版本号作为唯一真源（改一处自动传播到 Info.plist/GUI 显示/诊断列），billing_params_test T7/T7.1/T8 覆盖方案A/B、排除省、模板级/客户级双绑定。

## 目录

1. [项目概述](#1-项目概述)
2. [技术栈](#2-技术栈)
3. [项目结构](#3-项目结构)
4. [核心模块详解](#4-核心模块详解)
   - 4.1 [计算引擎](#41-计算引擎)
   - 4.2 [规则系统](#42-规则系统)
   - 4.3 [数据存储层](#43-数据存储层)
   - 4.4 [表头映射机制](#44-表头映射机制)
   - 4.5 [历史记录服务](#45-历史记录服务)
   - 4.6 [授权系统](#46-授权系统)
   - 4.7 [应用配置与性能调优](#47-应用配置与性能调优)
   - 4.8 [图标管理器](#48-图标管理器)
5. [数据库设计](#5-数据库设计)
   - 5.1 [SQLite 规则库](#51-sqlite-规则库)
   - 5.2 [SQLite 历史库](#52-sqlite-历史库)
   - 5.3 [DuckDB 计算引擎](#53-duckdb-计算引擎)
6. [运费计算逻辑详解](#6-运费计算逻辑详解)
7. [UI 界面设计](#7-ui-界面设计)
   - 7.1 [主窗口](#71-主窗口)
   - 7.2 [对话框总览](#72-对话框总览)
8. [授权系统详解](#8-授权系统详解)
9. [踩过的坑与经验教训](#9-踩过的坑与经验教训)
10. [部署与打包](#10-部署与打包)
11. [授权生成器工具](#11-授权生成器工具)
12. [附录：关键代码位置速查](#12-附录关键代码位置速查)

---

## 1. 项目概述

**小乔运费结算**是一款基于 Qt6 + DuckDB 开发的桌面运费计算软件，由 **杭州喵喵至家网络有限公司** 开发。

### 主要功能

| 功能模块 | 说明 |
|----------|------|
| 单条计算 | 手动输入省份、城市、重量、体积重等信息，快速计算运费明细 |
| 批量计算 | 支持 Excel/CSV/Parquet 文件批量导入，一键计算百万级数据并导出结果 |
| 对比分析 | 输入同一地址和重量，对比多套报价模板的价格差异 |
| 历史记录 | 记录每次批量计算的任务信息，支持查询、删除、查看、打开结果文件、清理旧数据 |
| 规则设置 | 运费模板管理、阶梯定价配置、燃油附加费、地区加价、自定义加价策略 |
| 客户规则设置 | 客户信息管理、客户专属报价表（矩阵格式）编辑、折扣设置、默认模板绑定 |
| 系统设置 | 自动性能优化（使用系统90%资源）、手动内存/线程配置、系统信息展示 |
| 快递模板识别管理 | 8 家快递自动识别模板的「总开关 + 逐条启用/禁用」、双击行可编辑、新增/删除，**自定义模板自动覆盖同ID内置模板** |
| 关于/授权 | 软件版本信息、机器码生成、授权码激活、授权有效期管理 |
| 品牌图标系统 | 全套「祥云快递盒」方案B：Mac App Bundle (.icns) / Dock / 所有窗口标题栏 / 关于页 Logo / QRC 资源 PNG 16-1024 |

### 核心特性

- ⚡ **高性能**：基于 DuckDB 列式存储引擎，百万级数据秒级计算
- 📊 **批量处理**：支持 Excel/CSV/Parquet 多种格式导入导出
- 🎯 **智能表头映射**：自动识别中英文表头（精确匹配优先 + 子串匹配兜底），缺失必填列时弹手动映射对话框
- 🔧 **灵活规则**：6 级重量阶梯、燃油附加费、地区加价（省/市/区三级）、自定义策略
- 🖥️ **自动性能调优**：启动时自动检测系统资源，使用 90% 内存和 CPU 核心数配置 DuckDB
- 🧠 **8 家快递模板自动识别 + 可视化管理**：内置中通/圆通/韵达/申通/极兔/邮政EMS/顺丰/德邦 模板指纹；「⚙️ 管理模板」入口支持启用禁用/双击编辑/增删；自定义模板与内置同ID 时**自定义优先覆盖**，避免改代码重启
- 🎨 **品牌项目图标全套生成**：按「祥云快递盒」方案B QPainter 程序化绘制 → 一键输出 Mac icns / 9 种尺寸 PNG / QRC 资源；Dock/Finder/窗口标题栏/Alt-Tab/关于页处处一致
- 🔐 **授权保护**：机器码绑定授权码、HMAC-SHA256 签名、防时间篡改、试用版/个人版/企业版/永久版多级别授权
- 📝 **历史可追溯**：每次批量计算自动保存记录，日期范围筛选、关键字搜索、旧数据清理

---

## 2. 技术栈

| 类别 | 技术 | 版本/说明 |
|------|------|-----------|
| 语言 | C++ | C++17 |
| GUI 框架 | Qt6 | 6.5+（Widgets，不含 QML） |
| Qt 模块 | Core / Gui / Widgets / Sql / Concurrent / Network | - |
| 构建系统 | CMake | 3.20+ |
| 规则数据库 | SQLite | 通过 Qt QSqlDatabase（WAL 模式） |
| 历史数据库 | SQLite | 独立 history.db |
| 计算引擎 | DuckDB | 1.5.4（含 excel 扩展） |
| 打包工具 | macdeployqt | Qt 自带 |
| 代码签名 | codesign | macOS 系统工具（ad-hoc 签名） |
| 哈希算法 | MD5 / HMAC-SHA256 | Qt QCryptographicHash |
| 配置存储 | INI 文件 | QSettings，config.ini |

---

## 3. 项目结构

```
xiaoqiao_freight/
├── CMakeLists.txt              # CMake 构建配置（主程序+授权生成器+gen_icon图标生成器+t7_test接口测试）
├── PROJECT.md                  # 项目文档（主文档）
├── PROJECT_v1.1.md             # v1.1 详细说明
├── PROJECT_v1.1_new.md         # 本项目文档（最完整最新版，含T7+图标系统说明）
├── PERFORMANCE_OPTIMIZATION.md # 性能优化专项文档
├── .gitignore                  # Git 忽略规则
├── resources/
│   ├── resources.qrc           # Qt 资源文件：注册 icons/png/logo_*.png 共9档(16→1024)
│   └── icons/
│       ├── xiaoqiao.icns       # Mac App Bundle 图标（392KB，10档尺寸，Finder/Dock预览）
│       ├── xiaoqiao.iconset/   # .icns 打包源：10档 icon_*@2x.png
│       └── png/                # 项目PNG资源：logo_16/24/32/48/64/128/256/512/1024.png
├── scripts/
│   └── deploy_mac.sh           # macOS 部署脚本（macdeployqt + 签名修复 + 去隔离属性）
├── tools/
│   ├── license_generator/      # 授权生成器工具（独立 GUI 程序）
│   │   ├── main.cpp
│   │   ├── license_generator_widget.hpp
│   │   └── license_generator_widget.cpp
│   ├── gen_icon/               # Logo 图标生成器（QGuiApplication + QPainter 代码绘制方案B，一键输出 .icns + PNG 全量）
│   │   └── main.cpp
│   ├── t7_test/                # 功能7模板识别 Headless 接口回归测试(5项/100%覆盖T7核心)
│   │   └── main.cpp
│   └── batch_runner/           # 批量计算非GUI入口
└── src/
    ├── main.cpp                # 程序入口（含 app.setWindowIcon -> app_logo 64x64）
    ├── core/                   # 核心类型与配置
    │   ├── app_config.hpp      # 应用配置（单例、性能调优、路径管理 + T7模板启用状态/自定义模板持久化）
    │   ├── app_config.cpp
    │   ├── freight_types.hpp   # 数据结构定义（枚举、CalcResult、SurchargeStrategy、TemplateFingerprint）
    │   ├── license_manager.hpp # 授权管理器（单例）
    │   └── license_manager.cpp
    ├── db/                     # 数据存储层
    │   ├── sqlite_rule_repository.hpp  # SQLite 规则库仓储
    │   ├── sqlite_rule_repository.cpp
    │   ├── duckdb_manager.hpp         # DuckDB 管理器（单例）
    │   └── duckdb_manager.cpp
    ├── services/               # 业务服务层
    │   ├── calc_service.hpp           # 计算服务（单条/批量/文件）
    │   ├── calc_service.cpp
    │   ├── template_recognizer.hpp    # T7 快递模板自动识别器（关键词+列名双重匹配，自定义模板优先覆盖内置）
    │   ├── template_recognizer.cpp
    │   ├── rule_service.hpp           # 规则服务（转发至 Repository，发 RulesChanged 信号）
    │   ├── rule_service.cpp
    │   ├── history_service.hpp        # 历史记录服务（增删查改、清理）
    │   └── history_service.cpp
    └── ui/                     # 界面层
        ├── main_window.hpp     # 主窗口
        ├── main_window.cpp
        ├── icon_manager.hpp    # 图标管理器（QPainter程序化绘制方案B + 名称+尺寸缓存，GenerateLogoIcon 新绘制「祥云快递盒」）
        ├── icon_manager.cpp
        └── dialogs/            # 对话框（12个）
            ├── single_calc_dialog.*     # 单条计算
            ├── batch_calc_dialog.*      # 批量计算（新增"⚙️ 管理模板"按钮→打开模板管理窗口）
            ├── compare_dialog.*         # 对比分析
            ├── history_dialog.*         # 历史记录
            ├── rule_setting_dialog.*    # 规则设置（多 Tab）
            ├── template_edit_dialog.*   # 运费价格模板编辑（阶梯价、分区、燃油）
            ├── courier_template_manager_dialog.*  # T7 快递识别模板管理：总开关+启用列复选框+增/改/删/批量启用禁用
            ├── courier_template_edit_dialog.*     # T7 快递识别模板编辑：ID/名称/快递/多行关键词/两列映射表
            ├── customer_setting_dialog.*# 客户设置 + 客户报价矩阵
            ├── system_setting_dialog.*  # 系统设置（性能）
            ├── header_mapping_dialog.*  # 表头手动映射
            └── about_dialog.*           # 关于 + 授权激活 + 品牌Logo
```

### 架构设计（三层架构）

```
┌──────────────────────────────────────────────────┐
│              UI 层 (Qt Widgets)                   │
│  MainWindow / 12 个 Dialog / IconManager          │
│ （+ CourierTemplateManager + CourierTemplateEdit）│
└──────────────────────┬───────────────────────────┘
                       ↓
┌──────────────────────────────────────────────────┐
│           Service 层 (业务逻辑)                    │
│  CalcService / RuleService / HistoryService /      │
│  TemplateRecognizer (T7-8家快递模板识别+去重)       │
└──────────────────────┬───────────────────────────┘
                       ↓
┌──────────────────────────────────────────────────┐
│           DB 层 (数据持久化 + config.ini)          │
│  SQLite (rules.db + history.db)                   │
│  DuckDB (calc.duckdb - 列式计算引擎)               │
│  AppConfig config.ini（模板启用开关 + 自定义模板） │
└──────────────────────────────────────────────────┘
```

**命名空间约定**：所有代码位于 `freight::xxx` 命名空间下
- `freight::core` — 核心配置与类型（含 T7 `TemplateFingerprint`）
- `freight::db` — 数据存储层
- `freight::services` — 业务服务层（含 `TemplateRecognizer` 模板识别）
- `freight::ui` — 界面层（含 `freight::ui::dialogs` 子命名空间，现 12 个对话框）
- `freight::tools` — 工具模块（授权生成器 / gen_icon 图标生成器 / t7_test 接口回归测试 / batch_runner 批量入口）

---

## 4. 核心模块详解

### 4.1 计算引擎

**文件**：[calc_service.cpp](file:///Users/cxd/duckdb/xiaoqiao_freight/src/services/calc_service.cpp)

计算引擎是整个系统的核心，基于 DuckDB 的向量化执行引擎实现高性能批量计算。

#### 核心方法

| 方法 | 说明 |
|------|------|
| `CalcSingle(province, weight, vol_weight, template_id, city, customer_id)` | 单条运费计算，返回 CalcResult 结构体 |
| `CalcBatch(input_table, output_table)` | 批量计算（DuckDB 表名 → DuckDB 表名） |
| `CalcFromFile(input_file, output_file)` | 从文件完整流程：导入→表头映射→计算→导出 |
| `BuildCalcSQL(input, output)` | 构建 10 层 CTE 的核心计算 SQL |
| `GetTableColumns(table)` | DESCRIBE 获取列名（替代不稳定的 information_schema） |
| `AutoMapColumns(actual_cols)` | 两轮表头自动映射（精确→子串） |
| `CreateNormalizedTable(input, mapping)` | 用 TRY_CAST + COALESCE 创建标准化输入表 |
| `GetPreviewHeaders/Rows(table, 5)` | 获取预览数据（表头映射对话框用） |

#### 计算 SQL 结构（10 层 CTE 链式调用）

批量计算使用一条完整的 SQL 完成，10 层 CTE 逐层计算：

```sql
CREATE OR REPLACE TABLE output_table AS
WITH
input_data AS (
    -- 1. 输入数据清洗
    --    COALESCE 处理空字符串 / NULL
    --    计算计费重量：charge_weight = max(weight, vol_weight)（vol_weight > 0 时）
),
customer_template_lookup AS (
    -- 2. 客户模板自动绑定
    --    LEFT JOIN customers 表，按 customer_id 取 default_template
    --    兜底：'zto_standard'
),
template_info AS (
    -- 3. 模板信息关联：获取 default_no_weight_fee 等模板参数
),
matched_zone AS (
    -- 4. 分区匹配
    --    REGEXP_REPLACE 去掉省份后缀（省/市/自治区等）
    --    LEFT JOIN zone_group_provinces + zone_groups
),
matched_tier AS (
    -- 5. 重量阶梯匹配（左开右闭）
    --    charge_weight > min_weight AND charge_weight <= max_weight
),
tier_max AS (
    -- 6. 最大阶梯兜底（超过最高阶梯时的回退参数）
    --    按 template_id + group_code 取 sort_order 最大的阶梯
),
base_fee_calc AS (
    -- 7. 基础运费计算
    --    0 或 NULL 重量 → default_no_weight_fee
    --    未匹配分区 → 0
    --    ≤ first_weight → first_price（一口价）
    --    匹配到阶梯 → first_price + charge_weight × additional_price
    --    超最高阶梯 → tier_max 兜底
),
fuel_surcharge_calc AS (
    -- 8. 燃油附加费计算
    --    LEFT JOIN fuel_surcharge（is_active=1）
    --    取 MAX(effective_date) ≤ CURRENT_DATE 的记录
    --    燃油费 = base_fee × rate
),
remote_area_calc AS (
    -- 9. 地区加价计算
    --    子查询 SUM(ra.surcharge)
    --    匹配规则：
    --      (1) 省匹配 + (市空或市匹配) + 区空 → 全省/全市
    --      (2) 市非空 + 市匹配 + 区空 → 全国该城市
),
strategy_surcharge_calc AS (
    -- 10. 其他附加费策略
    --    SUM(CASE strategy_type: fixed / percentage / per_weight)
    --    作用范围：global / province（关联 surcharge_provinces）
    --    重量过滤：min_weight ≤ charge_weight ≤ max_weight
    --    必须 is_active = 1
)
SELECT
    "订单号","客户编号","目的省份","目的城市",
    "实际重量(KG)","体积重量(KG)","计费重量(KG)",
    "基础运费","燃油附加费","地区加价","其他附加费","总运费","币种"
FROM strategy_surcharge_calc
```

#### 计费重量计算规则

```
if vol_weight > 0 且 vol_weight > weight:
    charge_weight = vol_weight
else:
    charge_weight = weight
```

体积重一般由外部计算后传入，模板参数 `vol_weight_ratio` 默认 6000，即 `长(cm)×宽(cm)×高(cm) / 6000`。

---

### 4.2 规则系统

**文件**：[sqlite_rule_repository.cpp](file:///Users/cxd/duckdb/xiaoqiao_freight/src/db/sqlite_rule_repository.cpp) + [rule_service.cpp](file:///Users/cxd/duckdb/xiaoqiao_freight/src/services/rule_service.cpp)

规则系统负责管理运费模板、分区、阶梯定价、附加费等配置数据，持久化存储在 SQLite 中。每次应用启动时，全量同步到 DuckDB 用于计算。

**RuleService** 是 Repository 的薄封装层，每次增删改操作后发射 `RulesChanged()` 信号，并通过 `main.cpp` 重新加载规则到 DuckDB。

#### 运费模板 (freight_templates)

每个模板代表一套完整的报价体系。

| 字段 | 说明 | 默认值 |
|------|------|--------|
| template_id | 主键，如 'zto_standard' / 'cust_xxx' | - |
| template_name | 模板名称，如 '中通标准快递' | - |
| carrier_name | 快递公司名称 | - |
| first_weight | 首重基准（KG） | 1.0 |
| additional_unit | 续重单位（KG） | 1.0 |
| vol_weight_ratio | 体积重系数 | 6000.0 |
| default_no_weight_fee | 重量缺失/0时的兜底运费 | 0 |
| is_default | 是否默认模板 | 0 |

内置默认模板：`zto_standard`（中通标准快递）

#### 分区组 (zone_groups + zone_group_provinces)

- 每个模板包含多个分区（zone1~zone6）
- 同一分区的所有省份共享同一套 6 级阶梯定价
- 通过 `sort_order` 控制显示顺序

**中通标准默认分区（6 区 31 省）**：

| 分区代码 | 分区名 | 省份数 | 省份列表 |
|----------|--------|--------|----------|
| zone1 | 一区 | 4 | 江苏、浙江、安徽、上海 |
| zone2 | 二区 | 10 | 山东、广东、福建、北京、河南、湖北、湖南、江西、天津、河北 |
| zone3 | 三区 | 10 | 山西、广西、四川、重庆、陕西、贵州、辽宁、吉林、黑龙江、云南 |
| zone4 | 四区 | 5 | 海南、甘肃、青海、内蒙古、宁夏 |
| zone5 | 五区-新疆 | 1 | 新疆 |
| zone6 | 五区-西藏 | 1 | 西藏 |

#### 阶梯定价 (tiered_pricing)

每个 (template_id, group_code) 下定义 6 个重量阶梯：

| 阶梯代码 | 名称 | 重量范围 | 计价方式 | first_price | additional_price |
|----------|------|----------|----------|-------------|------------------|
| tier_0_0.5 | 0-0.5KG | (0, 0.5] | 一口价 | 按分区 | **0** |
| tier_0.5_1 | 0.51KG-1KG | (0.5, 1.0] | 一口价 | 按分区 | **0** |
| tier_1_2 | 1-2KG | (1.0, 2.0] | 一口价 | 按分区 | **0** |
| tier_2_3 | 2-3KG | (2.0, 3.0] | 一口价 | 按分区 | **0** |
| tier_3_30 | 3-30KG | (3.0, 30.0] | 首重+续重 | 按分区 | 按分区 |
| tier_30_plus | 30KG以上 | (30.0, 9999] | 首重+续重 | 按分区 | 按分区 |

> **极其重要**：前 4 个阶梯（0-3KG）是纯一口价，`additional_price` **必须为 0**，否则计算公式 `first_price + charge_weight × additional_price` 会导致重复计费。这在 schema v10 升级时会自动修复。

#### 燃油附加费 (fuel_surcharge)

- 每个模板可有多条不同生效日期的记录（**无 UNIQUE 约束**，允许重复，但需注意数据清理）
- 计算时取 `effective_date ≤ CURRENT_DATE` 且 `is_active = 1` 的 `MAX(effective_date)` 一条
- `rate` 为小数，如 0.05 = 5%
- 计算方式：`燃油附加费 = 基础运费 × rate`

#### 地区加价 (remote_areas)

- 每个模板多条记录，省/市/区三级粒度
- `province` / `city` / `district` 均可为空（表示不限制）
- 匹配命中的记录 `SUM(surcharge)` 即为地区加价
- 支持启用/停用切换 `is_active`

**匹配优先级逻辑**（SQL 中的 OR 条件）：
1. 只填了 `province`：全省该省份匹配
2. 填了 `province` + `city`：全省该市匹配
3. 只填了 `city`（province 为空）：全国该名称的市匹配
4. 三级都填：精确到区县匹配

#### 自定义附加费策略 (surcharge_strategies + 关联表)

灵活的可扩展加价系统。

**作用范围 (strategy_scope)**：

| 值 | 含义 | 关联表 |
|----|------|--------|
| global | 对所有订单生效 | - |
| template | 对指定模板生效 | -（template_id 字段） |
| province | 对指定省份生效 | surcharge_provinces |
| customer | 对指定客户生效 | surcharge_customers |

**策略类型 (strategy_type)**：

| 值 | 计算公式 |
|----|----------|
| fixed | 固定 `amount` 元 |
| percentage | `base_fee × amount` |
| per_weight | `charge_weight × amount`（每公斤金额，按计费重量实重） |
| per_volume | `vol_weight × amount`（每公斤体积重金额，仅当体积重 > 0 且 > 实重时生效，否则 0） |

**模板绑定与作用范围的特殊规则（极其重要）**：
- `strategy_scope = 'global'` 或 `'template'`：**忽略 template_id 绑定**，对所有模板/所有客户专属模板均生效
  - 例：全局包装费 1 元、全局旺季加价 10%，即使"蜜丝婷专属报价"等客户模板也会被命中
- `strategy_scope = 'province' / 'customer'`：仍需 `s.template_id = 当前订单.template_id` 才匹配
- 历史兼容：SQL 层仍支持 `'template'` scope 值（与 global 等价），**新 UI 已移除该选项避免混淆**

**其他属性**：
- `priority`：优先级（目前仅排序，金额是 SUM 叠加）
- `min_weight / max_weight`：重量门槛（0 或 NULL 表示不限，数据库存 0 时加载为 NULL）
- `is_active`：启用开关
- `surcharge_date_ranges`：预留的日期范围 + 周几生效机制（暂未接入计算 SQL）

**内置默认策略 3 条**：
1. `packing_fee`：包装服务费，全局固定 1 元（template_id=zto_standard，因 global scope 忽略模板绑定对所有模板生效）
2. `remote_xz_xj`：新疆西藏地区加价，按重量 2 元/KG
3. `peak_season`：旺季附加费，全局 10%（可随时开关，因 global scope 忽略模板绑定对所有模板生效）

---

### 4.3 数据存储层

#### 双数据库 + 三库文件架构

| 数据库 | 库文件 | 用途 | 特点 |
|--------|--------|------|------|
| SQLite | `rules.db` | 运费规则数据持久化 | 事务性、WAL 模式、schema 版本化升级 |
| SQLite | `history.db` | 计算历史记录持久化 | 独立文件，支持分页查询、按日期筛选 |
| DuckDB | `calc.duckdb` | 批量计算引擎 + 临时表 | 列式存储、向量化执行、excel 扩展、内存限制/线程池 |

**应用启动数据流**：

```
main()
  ├─ AppConfig.Init()          # 创建 data_dir/{results,cache,logs}，读取 config.ini
  ├─ DuckDBManager.Init(calc.duckdb)
  │    └─ SET memory_limit / threads; INSTALL excel; LOAD excel
  ├─ SqliteRuleRepository(rules.db).Init()
  │    ├─ PRAGMA journal_mode=WAL / synchronous=NORMAL / foreign_keys=ON
  │    ├─ schema_version 检查与升级（当前 v11）
  │    ├─ CreateTables()
  │    └─ 首次运行时 InitDefaultData()（默认模板+默认策略）
  ├─ RuleService.InitDefaultData()
  ├─ DuckDBManager.LoadRulesFromSQLite(rules.db)
  │    ├─ 用 QSQLITE 连接打开 rules.db
  │    ├─ DROP + CREATE 所有 12 张规则表（结构同 SQLite，去掉 PK/自增）
  │    └─ 逐行读取 QSqlQuery → 手动拼 INSERT SQL → DuckDB
  └─ LicenseManager.Init()      # 读取授权文件 + 防篡改校验
```

每次应用启动都会从 SQLite **全量重建** DuckDB 中的规则表，确保数据一致。

#### DuckDB 管理器

**文件**：[duckdb_manager.cpp](file:///Users/cxd/duckdb/xiaoqiao_freight/src/db/duckdb_manager.cpp)

核心能力：
- `Init(db_path)` — 设置 `memory_limit`（系统内存 90%）、`threads`（CPU 核数 90%）、加载 `excel` 扩展
- `LoadRulesFromSQLite(path)` — 12 张表的 DROP→CREATE→INSERT 全流程
- `ReloadRules(path)` — 重载（规则变更后调用）
- `ImportFromFile(table, file)` — CSV/Parquet/XLSX → DuckDB 表
- `ExportToFile(table, file)` — DuckDB 表 → CSV/Parquet/XLSX（带表头）
- `GetRowCount(table)` / `TableExists(table)` — 元信息查询
- `CreateConnection()` — 为每次计算创建独立 `duckdb::Connection`

> **DuckDB 版本兼容 workaround**：DuckDB 1.5.4 中 `MaterializedQueryResult::GetValue<T>(col, row)` 模板方法存在类型转换 bug（小数截断），统一改为 `res->GetValue(col, row).GetValue<T>()` 先拿 Value 对象再转换。

---

### 4.4 表头映射机制

**文件**：[calc_service.cpp](file:///Users/cxd/duckdb/xiaoqiao_freight/src/services/calc_service.cpp) 的 `AutoMapColumns()`、`NormalizeColumns()`、`CreateNormalizedTable()`

批量计算的文件输入表头千变万化，映射机制保证正确关联到标准列。

#### 标准列定义（7 列）

| 标准列 | 中文名 | 必填 | 映射失败兜底 |
|--------|--------|:----:|------------|
| order_id | 订单号 | 否 | '' |
| customer_id | 客户编号/结算主体 | 否 | '' |
| shop_id | 店铺/结算店铺 | 否 | '' |
| dest_province | 目的省份 | ✅ 是 | 报错，弹手动映射对话框 |
| dest_city | 目的城市 | 否 | '' |
| weight | 实际重量 | ✅ 是 | 报错，弹手动映射对话框 |
| vol_weight | 体积重量 | 否 | 0 |

#### 映射关键字（每列多关键字，中英文混合，精确匹配优先防误伤）

| 标准列 | 关键字列表 |
|--------|-----------|
| order_id | order_id, order_no, waybill, 订单号, 订单编号, 运单号, 快递单号, 单号 |
| dest_province | dest_province, to_province, province, 目的省份, 省份, 收件省份, 到达省份, 收货省份 |
| dest_city | dest_city, to_city, city, 目的城市, 城市, 收件城市, 到达城市, 收货城市 |
| weight | weight, actual_weight, gross_weight, real_weight, 结算重量, 重量, 实际重量, 实重, 毛重, 计费重量 |
| vol_weight | vol_weight, volume_weight, volumetric_weight, 体积重量, 体积重, 体积, 抛重 |
| customer_id | customer_id, cust_id, customer, 客户id, 客户编号, 客户, 客户名称, 客户名, **结算对象, 结算主体, 结算公司** |
| shop_id | **shop_id, shop, store, 订单客户, 店铺, 结算店铺, 门店, 分公司** |

> **关键规则**：customer_id 的映射**不做"订单客户"子串匹配**，避免误伤；精确匹配轮次中 "订单客户" 只会命中 shop_id。客户模板匹配时，若 customer_id 为空则**兜底使用 shop_id** 匹配 customers 表的 default_template。

#### 两轮匹配策略（防误伤）

**第 1 轮（精确匹配，不区分大小写）**：
- 实际表头（小写） == 关键字（小写） → 命中
- 先完成所有列的精确匹配，避免子串误伤

**第 2 轮（子串匹配，仅对未匹配的标准列）**：
- 实际表头（小写） contains 关键字（小写） → 命中
- 典型场景："目的省份名称" 包含 "目的省份"

> 关键 case：避免 `订单客户` 被误匹配为 `customer_id`（因为包含 "客户"）。第 1 轮精确匹配优先保证这种列不会在 customer_id 下被挂接，除非没有其他更好的匹配。

#### 标准化输入表的类型安全

`CreateNormalizedTable()` 使用以下模式创建标准化列：

```sql
-- 字符串类：COALESCE 空值/空串 → ''
COALESCE("原列名", '') AS dest_province

-- 数值类：TRY_CAST 防止空串/非法值崩溃 → NULL → COALESCE → 0
COALESCE(TRY_CAST("原列名" AS DOUBLE), 0) AS weight
```

> 如果没有 `TRY_CAST`，`CAST('' AS DOUBLE)` 会抛异常中断整批计算。

---

### 4.5 历史记录服务

**文件**：[history_service.hpp](file:///Users/cxd/duckdb/xiaoqiao_freight/src/services/history_service.hpp) + [history_service.cpp](file:///Users/cxd/duckdb/xiaoqiao_freight/src/services/history_service.cpp)

每次批量计算完成后，由 UI（batch_calc_dialog）调用 `AddHistory()` 将任务元信息写入独立的 `history.db`。

#### 核心方法

| 方法 | 说明 |
|------|------|
| `Init()` | 打开 history.db，创建 calc_history 表 |
| `AddHistory(record)` | 新增一条记录，返回自增 id |
| `QueryHistory(page, page_size, keyword, date_from, date_to)` | 分页查询，支持关键字 + 日期范围过滤 |
| `GetHistory(id)` | 取单条详情 |
| `GetTotalCount()` | 记录总数 |
| `DeleteHistory(id)` / `DeleteHistoryList(ids)` | 单条/批量删除（事务） |
| `CleanupOldData(keep_days=90)` | 清理超过 90 天的旧记录，返回删除条数 |

#### 表结构：calc_history

| 字段 | 类型 | 说明 |
|------|------|------|
| id | INTEGER PK AUTOINCREMENT | 主键 |
| task_name | TEXT NOT NULL | 任务名（默认取输入文件名） |
| input_file | TEXT | 导入文件完整路径 |
| output_file | TEXT | 导出结果文件完整路径 |
| total_rows | INTEGER | 处理行数 |
| total_fee | REAL | 总运费金额（元） |
| duration_ms | INTEGER | 计算耗时（毫秒） |
| status | INTEGER | 1=成功，0=失败 |
| error_msg | TEXT | 失败错误信息 |
| created_at | DATETIME DEFAULT CURRENT_TIMESTAMP | 创建时间 |

---

### 4.6 授权系统

**文件**：[license_manager.hpp](file:///Users/cxd/duckdb/xiaoqiao_freight/src/core/license_manager.hpp) + [license_manager.cpp](file:///Users/cxd/duckdb/xiaoqiao_freight/src/core/license_manager.cpp)

完整的授权管理子系统，详见 **第 8 节 授权系统详解**。

---

### 4.7 应用配置与性能调优

**文件**：[app_config.hpp](file:///Users/cxd/duckdb/xiaoqiao_freight/src/core/app_config.hpp)

单例模式的全局配置中心。

#### 路径管理（基于 QStandardPaths::AppDataLocation）

macOS 下实际路径：`~/Library/Application Support/xiaoqiao_freight/`

| 方法 | 返回路径/值 |
|------|-----------|
| `GetDataDir()` | `~/.../xiaoqiao_freight/` |
| `GetRulesDbPath()` | `<data>/rules.db` |
| `GetHistoryDbPath()` | `<data>/history.db` |
| `GetResultsDir()` | `<data>/results/` |
| `GetCacheDir()` | `<data>/cache/` |
| `GetLogsDir()` | `<data>/logs/` |
| `GetLicenseFilePath()` | `<data>/license.dat`（授权码+TAMPER 持久化标记，多行） |
| `GetTimeRecordPath()` | `<data>/time_record.dat`（四段格式 last_start\|total_secs\|tampered\|adjusted_expire） |

#### 公司信息常量

| 方法 | 值 |
|------|---|
| GetCompanyName() | 杭州喵喵至家网络有限公司 |
| GetAppName() | 小乔运费结算 |
| GetWebsite() | www.hbdxm.com |
| GetServicePhone() | 17771300068 / 19171045360 |
| GetVersion() | 1.0.0 |

#### 系统信息采集（用于自动性能调优）

| 方法 | 说明 |
|------|------|
| `GetTotalMemoryMB()` | macOS：sysctlbyname `hw.memsize`；兜底 8192 |
| `GetCpuCoreCount()` | QThread::idealThreadCount；兜底 4 |

#### 性能配置（存储在 config.ini 的 [performance] 节）

| 配置项 | 说明 | 默认 |
|--------|------|------|
| auto | 自动性能优化开关 | true |
| memory_limit_mb | DuckDB 内存上限（MB） | 4096 |
| thread_count | DuckDB 计算线程数 | 4 |

**自动调优逻辑**（`ApplyAutoPerformance()`）：
```
memory_limit_mb = ROUND(系统总内存 MB × 0.9)
thread_count    = MAX(1, ROUND(CPU 核心数 × 0.9))
```
开启自动时，手动输入框在系统设置对话框中被禁用。

---

### 4.8 图标管理器 & 品牌图标系统 (方案B：祥云快递盒
**文件**：
- 运行时绘制：[icon_manager.hpp](file:///Users/cxd/duckdb/xiaoqiao_freight/src/ui/icon_manager.hpp) + [icon_manager.cpp](file:///Users/cxd/duckdb/xiaoqiao_freight/src/ui/icon_manager.cpp)
- 资产生成工具：[tools/gen_icon/main.cpp](file:///Users/cxd/duckdb/xiaoqiao_freight/tools/gen_icon/main.cpp)
- 打包注入：[CMakeLists.txt](file:///Users/cxd/duckdb/xiaoqiao_freight/CMakeLists.txt#L96-L109) (MACOSX_BUNDLE_ICON_FILE)
- 资源注册：[resources/resources.qrc](file:///Users/cxd/duckdb/xiaoqiao_freight/resources/resources.qrc)
- 应用入口设置全局窗口图标：[src/main.cpp](file:///Users/cxd/duckdb/xiaoqiao_freight/src/main.cpp#L50-L52)

#### 尺寸枚举 (IconSize)
SIZE_16 / SIZE_20 / SIZE_24 / SIZE_32 / SIZE_48 / SIZE_64

#### 分类枚举 (IconCategory)
CARD（64px 卡片大图）/ SETTING（24px 设置按钮）/ ACTION（16px 工具栏）/ STATUS（24px 状态）/ LOGO（32/64 窗口图标）

#### 便捷方法
```cpp
CardIcon("calc_single")     // 等价 GetIcon(..., CARD, SIZE_64)
SettingIcon("rule_setting") // 等价 GetIcon(..., SETTING, SIZE_24)
ActionIcon("save")          // 等价 GetIcon(..., ACTION, SIZE_16)
StatusIcon("success")       // 等价 GetIcon(..., STATUS, SIZE_24)
```

生成器方法：`GenerateCardIcon` / `GenerateSettingIcon` / `GenerateActionIcon` / `GenerateLogoIcon`（每个方法内部 switch(name) 用 QPainter 绘制对应图形）。

#### 全新 `GenerateLogoIcon()` 新版方案B 语义解析（代码重绘版
按 100% 程序化绘制（无任何资源依赖，任何尺寸即时重绘无锯齿：

| 视觉元素 | 坐标/颜色 | 语义 |
|---|---|---|
| 圆角方形底 | #4facfe → #00c6fb | 蓝→青 45°渐变 | 行业专业可信（蓝=物流/科技） |
| 快递盒 | 白底+#0d5cb8 深蓝线 | 物流快递=行业语义一眼认出是物流软件 |
| 黄色胶带 | #ffd86b 圆角矩形 | 盒盖封条，真实「封箱胶带 |
| 蓝底白字「乔」贴标 | #409eff 圆角方形贴 | 品牌识别品牌锚点=小乔品牌，中心居中 |
| 左上祥云 | 白色半透明三椭圆 | 祥云=小乔，暗示"小吉祥，双关=祥云=好运 |
| 右下小算盘 | 绿渐变#43e97b→#38f9d7 | 绿渐变绿=算账/盈利/运费结算正确 |
| 顶部柔光 | 顶部 58%不透明 | 让图标带立体玻璃感 |

Dock图标（App Bundle 注入：
- CMake 设置 `MACOSX_BUNDLE_ICON_FILE "xiaoqiao.icns"`，同时 `set_source_files_properties(... MACOSX_PACKAGE_LOCATION "Resources")` 把 `resources/icons/xiaoqiao.icns (392KB，复制进 Bundle 的 `.app/Contents/Resources/`，`Info.plist` 自动生成 `CFBundleIconFile = xiaoqiao.icns。
- Finder预览、Dock、Launchpad图标全部生效；Windows exe：
- `resources/icons/png/logo_256.png 可以直接用 imagemagick 或 Ncapconvert to `.ico` 注入 .exe 资源；

一键再生成：
```bash
cd build && cmake --build . --target gen_icon
./bin/gen_icon ../resources/icons/xiaoqiao.iconset
# 产出：
#   resources/icons/xiaoqiao.icns         #  Mac Bundle
#   resources/icons/png/logo_16~1024.png   # 资源/QT RCC 资源
```

---

### 4.9 快递模板自动识别 & 可视化管理（功能7 / T7）

**文件**：
- 模板指纹与持久化：[app_config.hpp](file:///Users/cxd/duckdb/xiaoqiao_freight/src/core/app_config.hpp) / [app_config.cpp](file:///Users/cxd/duckdb/xiaoqiao_freight/src/core/app_config.cpp)
- 识别与识别器(识别/去重/覆盖：[template_recognizer.hpp](file:///Users/cxd/duckdb/xiaoqiao_freight/src/services/template_recognizer.hpp) / [template_recognizer.cpp](file:///Users/cxd/duckdb/xiaoqiao_freight/src/services/template_recognizer.cpp)
- 管理对话框（列表+启用列+增改删）：[courier_template_manager_dialog.cpp](file:///Users/cxd/duckdb/xiaoqiao_freight/src/ui/dialogs/courier_template_manager_dialog.cpp)
- 编辑对话框（点开编辑ID/名称/快递/关键词/列映射）：[courier_template_edit_dialog.cpp](file:///Users/cxd/duckdb/xiaoqiao_freight/src/ui/dialogs/courier_template_edit_dialog.cpp)
- 入口按钮：[batch_calc_dialog.cpp](file:///Users/cxd/duckdb/xiaoqiao_freight/src/ui/dialogs/batch_calc_dialog.cpp)（批量计算页"⚙️ 管理模板"

#### 核心数据结构：TemplateFingerprint
```cpp
struct TemplateFingerprint {
    QString   template_id;
    QString   display_name;
    QString   courier_name;
    QStringList required_keywords; // 内容关键词（"中通"|"顺丰特惠）
    QMap<QString, QString> column_mapping; // 列映射：Excel列名 → 标准列(order_id/dest_province/weight 等
    bool      enabled = true;
    bool      is_builtin = false; // 内置 vs 自定义
};
```

#### 内置8家内置模板
中通 / 圆通 / 韵达 / 申通 / 极兔 / 邮政EMS / 顺丰 / 德邦 各有 6个指纹：每个模板必须≥1条关键词 ≥2组列映射保证可落地。

#### 持久化 (config.ini 键
- `[Templates/disabled_ids = QStringList` — 单条禁用列表
- `[Templates]/auto_detect_global = true/false` — 总开关
- `[Custom_Templates]` 小节：所有自定义模板。
- `AppConfig::GetAllTemplateFingerprints(enabled_only)` — 拉取时合并 内置+自定义，并按enabled过滤。

#### 识别器算法核心算法：关键词+列名双重匹配（合并打分：
1. 先过滤掉 `GetAllTemplateFingerprints(true)` **已启用**的模板。
2. **去重覆盖**：相同template_id若有自定义模板时，**丢弃同ID。
3. 样本前5行内容中关键词命中次数权重 × 0.6 + Excel表头列命中文精确/子串命中权重×0.4；分数最高且大于阈值才返回。
4. 返回 `TemplateMatchResult` {matched/template_id/match_score`。

#### UI：列表页能力：
- 顶部：总开关（一键全启用全禁用；
- 每行：启用复选框勾选立生效；
- 双击任意非第0列=行打开编辑对话框；
- 选中1行：选中/编辑/删除/全部启用/全部禁用；
- 删除内置模板会："系统内置模板不可删除，①取消勾选禁用 ②编辑生成自定义覆盖版"提示；
- 顶部统计：总数 N / M（已启用数 / 内置数 / 自定义数 + M自定义数。

编辑对话框校验规则：
```cpp
- template_id 内置不可编辑仅允许ID锁存自定义可改（内置ID上锁🔒
- 必填：display_name / 空，关键词自动；
- 关键词多行输入 ，自动 分割顿号逗号/分;
- 列映射表：≥至少2组，不允许重重复Excel列名
- 保存时：写入到 AppConfig::AddCustomTemplateFingerprint 或 UpdateCustomTemplateFingerprint；若是内置被保存=自动 另存为自定义覆盖版（is_builtin=false）。

Headless 接口回归测试工具：tools/t7_test/main.cpp 5项100%覆盖：
1. 默认 ≥8内置模板合法；
2. 单条禁用+全局开关读写；
3. 自定义模板同ID覆盖内置；
4. CRUD；
5. 持久化写盘重启一致。

---

## 5. 数据库设计

### 5.1 SQLite 规则库

**文件**：`AppDataLocation/rules.db`（连接名 `rules_conn_N`）

#### Schema 版本管理

通过 `schema_version(version INTEGER)` 表控制升级流程，当前版本 **11**。

| 版本 | 变更内容 |
|------|---------|
| < 10 | 大版本升级：关闭外键 → 手动 DROP 全部 11 张表 → 重建（避免 WAL 下 DROP 不彻底的约束残留） |
| v10 → v11 | `ALTER TABLE freight_templates ADD COLUMN default_no_weight_fee REAL DEFAULT 0` |

v10 升级时额外操作：
```sql
-- 修复一口价阶梯续重价错误为 0
UPDATE tiered_pricing SET additional_price = 0
WHERE tier_code IN ('tier_0_0.5', 'tier_0.5_1', 'tier_1_2', 'tier_2_3');
```

#### 12 张规则表清单

| 表名 | 主键 | 核心字段说明 |
|------|------|------------|
| **customers** | customer_id | customer_name, discount_rate(REAL), default_template, contact_person, contact_phone, address |
| **freight_templates** | template_id | template_name, carrier_name, first_weight, additional_unit, vol_weight_ratio, **default_no_weight_fee**, is_default, description |
| **zone_groups** | id AUTOINCREMENT | UNIQUE(template_id, group_code); group_name, sort_order |
| **zone_group_provinces** | id AUTOINCREMENT | UNIQUE(template_id, group_code, province); 3 列外键关联 |
| **tiered_pricing** | id AUTOINCREMENT | UNIQUE(template_id, group_code, tier_code); min_weight/max_weight/first_weight/first_price/additional_unit/additional_price/sort_order |
| **fuel_surcharge** | id AUTOINCREMENT | template_id, effective_date(DATE), rate(REAL), is_active(INT DEFAULT 1) |
| **remote_areas** | id AUTOINCREMENT | template_id, province, city, district, surcharge(REAL), is_active(INT DEFAULT 1) |
| **surcharge_strategies** | id AUTOINCREMENT | UNIQUE(strategy_id); strategy_name, strategy_scope, template_id, strategy_type, amount, min_weight, max_weight, priority, is_active, description |
| **surcharge_provinces** | id AUTOINCREMENT | UNIQUE(strategy_id, province) |
| **surcharge_customers** | id AUTOINCREMENT | UNIQUE(strategy_id, customer_id) |
| **surcharge_date_ranges** | id AUTOINCREMENT | strategy_id, start_date, end_date, week_days |
| **schema_version** | - | version(INT) |

> ⚠️ **注意**：`fuel_surcharge` 和 `remote_areas` 没有 `UNIQUE(template_id, effective_date/...)` 约束，允许重复记录。但 LEFT JOIN 时多行匹配会导致结果行倍增 → 金额翻倍。注意维护数据唯一性。

---

### 5.2 SQLite 历史库

**文件**：`AppDataLocation/history.db`（连接名 `history_conn`）

| 表名 | 结构 |
|------|------|
| calc_history | 见 4.5 节 |

HistoryService 懒加载：首次 `AddHistory` / `QueryHistory` 时才调用 `Init()` 建表。

---

### 5.3 DuckDB 计算引擎

**文件**：`AppDataLocation/calc.duckdb`

#### 规则表（每次启动重建，共 12 张）

结构与 SQLite 规则库基本一致，但：
- 无 PRIMARY KEY / AUTOINCREMENT / UNIQUE 约束
- 所有整型用 INTEGER / DOUBLE / VARCHAR / TIMESTAMP / DATE
- 从 SQLite 逐行读取后手动拼 INSERT（避免 SQLite ATTACH 的 DECIMAL/REAL 类型混淆）

#### 启动时自动执行的 DuckDB PRAGMA

```sql
SET memory_limit = '<90% 系统内存 MB>MB';
SET threads      = <90% CPU 核数>;
INSTALL excel;
LOAD excel;       -- 提供 read_xlsx() + COPY xlsx 能力
```

#### 文件导入导出能力

| 格式 | 导入 | 导出 |
|------|------|------|
| CSV | `read_csv(file, AUTO_DETECT=TRUE, HEADER=TRUE)` | `COPY TO (FORMAT CSV, HEADER TRUE)` |
| Parquet | `read_parquet(file)` | `COPY TO (FORMAT PARQUET)` |
| XLSX/XLS | `read_xlsx(file, header=true)` | `COPY TO (FORMAT xlsx, HEADER TRUE)` |

#### 临时表命名约定

| 表名 | 用途 |
|------|------|
| `_input_tmp` | CalcFromFile 中刚导入的原始表 |
| `_input_normalized` | 表头映射 + 类型标准化后的计算输入表 |
| `_output_tmp` | CalcFromFile 中计算完成待导出的结果表 |

---

## 6. 运费计算逻辑详解

### 6.1 总公式

```
总运费 = 基础运费
      + 燃油附加费
      + 地区加价
      + Σ 其他附加费（策略）
```

### 6.2 基础运费计算（7 分支 CASE）

| 分支条件 | 结果 |
|----------|------|
| charge_weight ≤ 0 或 NULL | `default_no_weight_fee`（模板参数，默认 0） |
| group_code 未匹配（省份不在任何分区） | 0 |
| charge_weight ≤ matched_tier.first_weight | `matched_tier.first_price`（一口价） |
| 匹配到 tier_code（命中任意阶梯） | `first_price + charge_weight × additional_price` |
| 以上都不满足（超过最高阶梯） | `tier_max.first_price + charge_weight × tier_max.additional_price`（兜底） |

> **边界匹配规则（左开右闭，极其重要）**：
> ```sql
> charge_weight > min_weight AND charge_weight <= max_weight
> ```
> 例：0.5kg 落入 tier_0.5_1，3kg 落入 tier_3_30，30kg 落入 tier_30_plus。

### 6.3 燃油附加费

```sql
LEFT JOIN fuel_surcharge fs
  ON fs.template_id = bfc.template_id
 AND fs.is_active = 1
 AND fs.effective_date = (
     SELECT MAX(effective_date) FROM fuel_surcharge
      WHERE template_id = bfc.template_id
        AND is_active = 1
        AND effective_date <= CURRENT_DATE
 )
燃油附加费 = COALESCE(fs.rate, 0) × base_fee
```

### 6.4 地区加价

子查询中 SUM 所有匹配的 remote_areas 记录：
```
匹配条件（OR）：
  A. province 非空 AND province = 归一化省份
     AND (city 空 OR city = dest_city) AND district 空
  → 全省加价，或全省内指定城市

  B. city 非空 AND city = dest_city AND district 空
  → 全国该城市（不看省份）
```

多条匹配时 SUM(surcharge)；无匹配 → 0。

### 6.5 其他附加费策略

SUM 所有匹配策略的 CASE 表达式：
```sql
CASE s.strategy_type
    WHEN 'fixed'      THEN s.amount
    WHEN 'percentage' THEN base_fee * s.amount
    WHEN 'per_weight' THEN charge_weight * s.amount
    WHEN 'per_volume' THEN CASE WHEN vol_weight > 0 AND vol_weight > weight
                                THEN vol_weight * s.amount ELSE 0 END
    ELSE 0
END
```

**策略匹配条件（2026-07-25 修复，极其重要）**：

```sql
WHERE s.is_active = 1
  -- 条件A：global/template scope 忽略模板绑定，对所有模板生效（包装费、旺季加价等）
  AND (s.strategy_scope IN ('global', 'template') OR s.template_id = rac.template_id)
  -- 条件B：作用范围二级匹配（省/客户级需要进一步命中关联表）
  AND (
      s.strategy_scope IN ('global', 'template')
      OR (s.strategy_scope = 'province' AND 归一化省份匹配)
      OR (s.strategy_scope = 'customer' AND sc.customer_id = rac.customer_id)
  )
  -- 条件C：重量范围
  AND (s.min_weight IS NULL OR charge_weight >= s.min_weight)
  AND (s.max_weight IS NULL OR s.max_weight = 0 OR charge_weight <= s.max_weight)
```

> ❌ 旧 bug：条件A 写成 `s.template_id = rac.template_id`，导致"蜜丝婷专属报价"等客户模板无法命中 zto_standard 模板下的包装费/旺季加价（因为 template_id 不同）。修复后 global/template scope 跳过模板绑定。

单条计算 SQL 同逻辑，参数化后分别使用 `'%1'=template_id`、`'%2'=省份归一化`、`'%5'=customer_id` 占位。

---

## 7. UI 界面设计

### 7.1 主窗口

**文件**：[main_window.cpp](file:///Users/cxd/duckdb/xiaoqiao_freight/src/ui/main_window.cpp)

尺寸：900×650，屏幕居中。

布局结构（自上而下）：

```
┌──────────────────────────────────────────────────┐
│  ┌─────────────────────────────────────────────┐ │
│  │ ★ 顶部广告横幅（渐变紫蓝，圆角12px，高100px）│ │
│  │   大字标题（每 4 秒轮播 4 条）                │ │
│  │   客服热线小字                               │ │
│  └─────────────────────────────────────────────┘ │
│                                                    │
│  ┌─────────────────────────────────────────────┐ │
│  │  单条计算  │  批量计算    ← 2×2 卡片网格     │ │
│  │  (64px图标) │  (64px图标)   白底+灰边框      │ │
│  │─────────────┼─────────────  悬停变蓝         │ │
│  │  对比分析  │  历史记录    padding 30px       │ │
│  │  (64px图标) │  (64px图标)   min-height 180  │ │
│  └─────────────────────────────────────────────┘ │
│                                                    │
│  ┌─────────────────────────────────────────────┐ │
│  │ [规则设置] [客户规则设置] [系统设置] ... [关于] │ │
│  │  ← 白底圆角，灰底按钮，悬停变蓝               │ │
│  └─────────────────────────────────────────────┘ │
│                                                    │
│     杭州喵喵至家网络有限公司 © 2026 | www.hbdxm.com │
│                     ↳ 蓝色下划线外链               │
└──────────────────────────────────────────────────┘
```

**QSS 主题色**：主色 `#409eff`（element-ui 蓝），背景 `#f5f7fa`，卡片白底 `#ffffff`。

**启动后检查**：QTimer 300ms 延迟调用 `CheckLicenseStartup()`，检查授权是否过期/即将过期，并弹 MessageBox 警告。

**功能访问控制**：批量计算入口会先检查 `LicenseManager.IsFunctionAvailable()`，未授权/过期弹提示，不打开对话框。

---

### 7.2 对话框总览

| 对话框类 | 文件名 | 功能要点 |
|----------|--------|---------|
| **SingleCalcDialog** | single_calc_dialog.* | 单条计算：订单号/省份（下拉）/城市/实重/体积重/模板（下拉）。结果显示计费重量/基础运费/燃油/地区加价/其他附加费/总运费。按钮：计算 / 清空 / 关闭。 |
| **BatchCalcDialog** | batch_calc_dialog.* | QStackedWidget 两页：(1) 文件选择页 — 选输入输出文件、进度条、开始按钮（白底+灰边框+悬停变蓝，padding 12px×36px，字号 14px，尺寸大于关闭按钮）；(2) 结果预览页 — QTableWidget 前 N 行、结果汇总（总行数/总运费/耗时）、导出、返回。计算结束自动调用 HistoryService.AddHistory()。 |
| **CompareDialog** | compare_dialog.* | 选省份/实重/体积重，点击"对比"后遍历所有模板计算，表格展示各模板的基础运费/燃油费/地区加价/附加费/总运费对比。 |
| **HistoryDialog** | history_dialog.* | 顶部：关键字搜索框 + 起始日期（默认一个月前）+ 结束日期（默认今天）+ 搜索按钮。中部：QTableWidget 任务列表（任务名/输入文件/输出文件/行数/总运费/耗时/状态/创建时间）。底部：打开文件/导出/选中删除/清理旧数据(90天)/关闭。 |
| **RuleSettingDialog** | rule_setting_dialog.* | QTabWidget 5 个 Tab：(1) 运费模板列表（增删改→打开 TemplateEditDialog）；(2) **加价策略**：顶部模板筛选下拉（默认当前模板），表格列含策略ID/名称/作用范围/类型/金额/优先级/启用；新增/编辑对话框：作用范围下拉保留「全局/省份级/客户级」三项（**已移除「模板级」**，与全局语义重复易混淆；历史 template scope 数据编辑时自动降级为 global）；策略类型下拉含「固定加价/比例加价/按重量/**按体积(元/kg 体积重)**」四项；金额显示格式：fixed=¥xx.xx / percentage=xx.x% / per_weight=¥xx/kg / per_volume=¥xx/kg 体积重；(3) **燃油附加费**：顶部模板筛选下拉，表格含模板/生效日期/费率/启用 toggle，新增/编辑对话框含模板 ComboBox 选择；(4) **地区加价**：顶部模板筛选下拉，省/市/区三级匹配，新增/编辑对话框含模板 ComboBox 选择（至少填写省/市/区之一即可）；(5) 预留。 |
| **TemplateEditDialog** | template_edit_dialog.* | 编辑单个模板的完整配置：Tab1 基本信息（名称/快递/首重/续重/体积系数/无重量默认费/默认/描述）；Tab2 阶梯价格表（编辑各分区各阶梯价格）；Tab3 分区-省份关联（调整分区包含哪些省）；Tab4 燃油附加费（同规则设置中的 Tab）。 |
| **CustomerSettingDialog** | customer_setting_dialog.* | 左侧 QListWidget 客户列表（增删改/批量导入）；右侧客户专属报价矩阵表格：10 列表头（报价区域/目的省份/0-0.5KG/0.5-1KG/1-2KG/2-3KG/3-30KG/30KG+/其他/备注）。报价区域列合并单元格，同分区省份连续排列；区域名和省份名只读，价格列双击可编辑；编辑某省份价格时自动同步同分区其他省份；保存时按分区去重写入 tiered_pricing。底部：保存报价按钮。 |
| **SystemSettingDialog** | system_setting_dialog.* | Tab1 性能设置：☑ 自动优化性能（使用系统90%资源）默认勾选；系统信息展示（总内存/CPU核数/自动计算后的内存限制和线程数）；手动内存 MB + 线程数 SpinBox（自动模式下禁用）；Tab2 预留。确定/取消按钮。 |
| **HeaderMappingDialog** | header_mapping_dialog.* | 当 AutoMap 缺失必填列时弹出。左列导入表头 + 右列标准列（必填标红星 *）+ 红色箭头连线可视化映射；点击交互调整；底部 QTableWidget 预览前 5 行数据。确认后返回自定义 mapping。 |
| **AboutDialog** | about_dialog.* | 品牌 Logo（祥云快递盒方案B 64x64）、应用名 + "v1.0.0"、公司名、官网（蓝色下划线）、客服电话。授权信息区：机器码（X-XXX-XXX-XXX-XXX，带一键复制按钮）、授权状态（试用版/个人版/企业版/永久版）、有效期（剩余天数）、"授权激活"按钮（弹输入框输入授权码 → LicenseManager.ActivateLicense()）。 |
| **CourierTemplateManagerDialog** | courier_template_manager_dialog.* | T7 快递识别模板管理页：顶部「启用快递模板自动识别」全局开关 + 状态统计(总数/启用/内置/自定义)；列表首列"启用"复选框勾选立即生效；双击行打开编辑；按钮：新增/编辑/删除(内置防删)/全部启用/全部禁用；发射 TemplatesChanged 信号同步主界面自动识别勾选框状态。 |
| **CourierTemplateEditDialog** | courier_template_edit_dialog.* | T7 快递识别模板编辑页：Tab1 基本信息(内置ID锁🔒自定义可改/名称/快递公司)；Tab2 关键词多行输入，顿号逗号换行自动去重；Tab3 列映射表(Excel原始列→系统标准列)增删改、校验≥2组、不允许重复Excel列；保存时写入自定义模板(内置编辑→生成自定义覆盖版)。 |

---

## 8. 授权系统详解

### 8.1 架构概览

```
┌─────────────────────────────────────────────────────┐
│  LicenseManager（主程序内单例）                       │
│    ├─ 四维机器码生成（主板+CPU+系统盘+网卡 → MD5）   │
│    ├─ 授权码验证（Base64 JSON + HMAC-SHA256 签名）   │
│    ├─ 加密存储（license.dat 保存授权码+篡改标记）    │
│    ├─ 首次启动自动创建30天试用版授权码并落盘          │
│    ├─ 启动验证 + 过期/临期弹窗提醒                   │
│    ├─ 防时间篡改双文件校验（license.dat+time_record.dat）│
│    └─ 时间篡改扣减有效期持久化（重启仍生效）          │
└─────────────────────────────────────────────────────┘
                           ↕ 相同算法
┌─────────────────────────────────────────────────────┐
│  授权生成器（独立 GUI 程序 license_generator.app）    │
│    ├─ 输入：机器码 + 授权类型 + 有效期 / 天数        │
│    ├─ 输出：格式化授权码（可复制）                    │
│    └─ 支持校验：粘贴授权码 + 机器码验证签名          │
└─────────────────────────────────────────────────────┘
```

### 8.2 机器码生成（四维硬件组合，抗漂移）

**跨平台采集优先级**（主板→CPU→系统盘→网卡）：

| 维度 | macOS 实现 | Windows/Linux 兜底 |
|------|-----------|-------------------|
| 主板 UUID | `ioreg -c IOPlatformExpertDevice -d 2 \| grep IOPlatformUUID` | WMI Win32_BaseBoard / DMI /sys/class/dmi/id |
| CPU ID | `sysctl -n machdep.cpu.signature` + brand | WMI Win32_Processor / /proc/cpuinfo |
| 系统盘卷 UUID | `diskutil info / \| grep "Volume UUID"`（根分区） | WMI Win32_LogicalDisk(C:) / lsblk -f |
| 网卡 MAC | 只取**第一张稳定**的有线(Ethernet)/WiFi硬件地址，跳过虚拟/网桥/隧道 | 同左，取 en0/eth0 第一张非虚拟 |

**算法流程**：
```
1. 依次采集四个维度的硬件字符串（每维读取失败则用该维的常量兜底值）
2. 若前四维全部失败：使用保底值 = 系统信息(OS名+版本) + CPU架构 + 所有非回环MAC
3. 若仍全空：最终保底 = "XIAOQIAO_DEFAULT_MACHINE"
4. raw = 主板串 + "|" + CPU串 + "|" + 盘串 + "|" + MAC串
5. QCryptographicHash::Md5(UTF8(raw)) → 32 位 HEX → 大写
6. 按每 4 字符加分隔符：XXXX-XXXX-XXXX-XXXX（共 35 字符）
```

例：`A1B2-C3D4-E5F6-7890`

> **授权生成器同步要求**：机器码算法变更后，必须同步重新编译打包 `license_generator.app`，确保与主程序使用同一套源码；**旧算法生成的授权码无法在新版客户端使用**，老用户升级需基于新机器码重新生成。

### 8.3 授权码结构（HMAC-SHA256 签名 + Base64）

```
授权码 = 格式化( Base64(JSON 载荷) + "." + HEX(HMAC-SHA256) )
```

**JSON 载荷（3 字段）**：
```json
{ "mc":   "A1B2C3D4E5F67890",   // 机器码，去掉 '-'
  "type": 0|1|2|3,              // 授权类型枚举
  "exp":  "20270131235959" }    // 到期时间 yyyyMMddHHmmss
```

**HMAC 密钥**（硬编码常量）：`XiaoQiaoFreight2026@#$SecretKey888`

**HMAC 计算**：
```
HEX( HMAC-SHA256( UTF8(JSON 紧凑序列化), 密钥 ) ) → 大写
```

**最终拼接 & 格式化**：
```
combined = Base64(JSON_payload) + "." + HMAC_HEX
每 6 字符加 "-"，如 XXXXX1-XXXXX2-...-XXXXXn
```

### 8.4 授权类型

| 枚举值 | 名称 | 含义 | 过期检查 |
|--------|------|------|---------|
| 0 | 试用版 Trial | 首次启动时自动创建，30 天 | 30 天后过期 |
| 1 | 个人版 Personal | 付费个人授权 | 按 expire_date |
| 2 | 企业版 Enterprise | 付费企业授权 | 按 expire_date |
| 3 | 永久版 Permanent | 终身授权 | 永不过期 |

### 8.5 验证流程

```
用户粘贴授权码 → 去 "-" → split(".") 成两段
  ↓ 长度 != 2 → "授权码格式错误"
  ↓ Base64 解码载荷 + HEX 比较 HMAC
    ↓ 不一致 → "授权码验证失败"
    ↓ 解析 JSON → 拿 mc/type/exp
      ↓ mc != 本机机器码(去 "-") → "机器码不匹配"
      ↓ exp 解析 → 得到 expire_date
        ↓ 当前时间 > expire_date 且 type != Permanent → "授权已过期"
          ↓ 全部通过 → 授权成功！
            保存到 license.dat → activated_=true
```

### 8.6 首次启动试用版授权自动落盘（防无限续期）

行为：当 `license.dat` 不存在时（首次安装或用户手动删除），**立即生成**：
```
试用授权 = GenerateLicenseKey(本机机器码, Trial, 今日+30天)
```
并**立刻写入** `license.dat`。授权码自带 HMAC 签名，用户无法通过删除缓存/重启重复获取试用。

> ❌ 旧 bug 修复：此前仅在内存中设置 expire 而不落盘，用户可通过删除 `license.dat` 反复获取 30 天试用。

### 8.7 防时间篡改（双文件交叉校验 + 扣减持久化）

**双文件存储策略**：

| 文件 | 内容 | 作用 |
|------|------|------|
| `license.dat` | 第1行=授权码；第2行起=持久化标记行（如 `TAMPER=1`） | 授权本体 + 永久篡改锁 |
| `time_record.dat` | 四段格式：`<last_start>|<total_run_secs>|<tampered(0/1)>|<adjusted_expire>` | 上次启动时间 + 篡改状态 + **扣减后的有效期覆盖值** |

向后兼容：支持 2-3 段旧格式（缺 tampered 设 0，缺 adjusted_expire 设空）。

**时间篡改判定**：
- `当前时间 < last_start` → 判定回拨，`tampered=1`
- 篡改双写：`time_record.dat` 中 `tampered=1` **同时**在 `license.dat` 末尾追加 `TAMPER=1` 行（双文件，删一个另一个仍锁死）
- 若只删 `time_record.dat`，下次启动读取 `license.dat` 的 `TAMPER=1` 行仍判定篡改，**重新写回** `time_record.dat tampered=1`

**有效期扣减与持久化（关键）**：
- 非永久版：按回拨天数（⌈秒数/86400⌉）扣减 `expire_date`，**扣减结果存入 `adjusted_expire` 字段** → 写回 `time_record.dat`
- 重启后读取：若 `adjusted_expire` 存在且比新授权更紧，**保留扣减后的有效期**（防止用户改时间→白嫖→激活新授权重置）
- 永久版激活：清除 `TAMPER=1` 锁 + 篡改状态 + adjusted_expire，恢复干净状态

> ❌ 旧 bug 修复：此前 SaveLicense 只写回原 license_key，adjusted_expire 未持久化导致重启恢复；删除 time_record.dat 可清零篡改痕迹。

### 8.8 授权有效性与剩余天数

- `DaysRemaining()`：**无效授权返回 -1**（非 0），避免误判"剩余 0 天即将到期"
- `IsNearExpiry(remaining<=7)`：仅对 `valid=true` 且 `remaining>=0` 生效，无效授权直接返回 false

### 8.9 启动提醒分级（CheckStartupReminder）

| 状态 | show_expired | show_near_expiry | 提示内容 |
|------|:-----------:|:---------------:|---------|
| time_tampered（双文件任一命中） | ✅ | - | 检测到系统时间被篡改 + 购买正版提示 |
| 授权已过期（已激活 + adjusted_expire 生效） | ✅ | - | 授权已过期 + 续费电话 |
| 试用期结束（未激活，落盘的 Trial 过期） | ✅ | - | 试用期结束 + 购买电话 |
| 剩余天数 ≤ 7（已激活，valid=true, remaining>=0） | - | ✅ | N 天后到期 + 续费电话 |
| 剩余天数 ≤ 7（未激活的 Trial） | - | ✅ | 试用期还剩 N 天 + 购买电话 |

---

## 9. 踩过的坑与经验教训

### 9.1 DuckDB 相关

#### ❌ 坑 1：MaterializedQueryResult::GetValue<T>() 模板方法 bug
**现象**：`res->GetValue<int64_t>(0,0)` 截断小数、错误判断类型。  
**原因**：DuckDB 1.5.4 C++ API 模板实现缺陷。  
**解决**：先拿 `Value` 对象再转 → `res->GetValue(0,0).GetValue<int64_t>()`。本项目统一使用此模式。

#### ❌ 坑 2：read_excel 不存在
**现象**：导入 Excel 报 `Function name 'read_excel' does not exist`。  
**原因**：DuckDB 1.5 中 Excel 函数叫 `read_xlsx()`，且需要独立安装 excel 扩展。  
**解决**：启动时 `INSTALL excel; LOAD excel;`，使用 `read_xlsx(path, header=true)`。

#### ❌ 坑 3：information_schema.columns 不可靠
**现象**：偶尔返回空列表，导致表头映射失败。  
**解决**：改用 `DESCRIBE SELECT * FROM table_name` 获取列名，更稳定。

#### ❌ 坑 4：CAST 空串崩溃
**现象**：体积重列为空字符串时，CAST 报错。  
**解决**：用 `TRY_CAST` 转 + `COALESCE(..., 0)` 兜底。

### 9.2 计算逻辑相关

#### ❌ 坑 5：一口价阶梯 additional_price 设错
**现象**：2KG 运费显示 7.12，期望 3.56。  
**原因**：前 4 个阶梯 additional_price = first_price，公式 first_price + weight × additional_price 变成双倍。  
**解决**：强制 tier_0_0.5/tier_0.5_1/tier_1_2/tier_2_3 的 additional_price = 0。schema v10 升级自动修复。

#### ❌ 坑 6：fuel_surcharge 多行导致行倍增
**现象**：同一模板多条相同生效日期，LEFT JOIN 一次变 N 行，运费翻倍。  
**教训**：允许写入重复但提供前端 UI 管理，或未来加子查询取 MAX(id) 去重。

#### ❌ 坑 7：customer_id 子串匹配误伤
**现象**："订单客户" 列被匹配为 customer_id。  
**解决**：两轮匹配（先精确、后子串）。

### 9.3 SQLite 相关

#### ❌ 坑 8：WAL 下 DROP TABLE 后 UNIQUE 约束残留
**现象**：C++ 中 DROP 后重建同名表，旧 UNIQUE 仍起作用。  
**解决**：schema v10 升级关外键后按清单手动逐个 DROP，或走 sqlite3 CLI 更可靠。

### 9.4 打包部署相关（macOS）

#### ❌ 坑 9：两套 Qt 库冲突
**现象**：启动报 cocoa 插件加载失败，控制台输出两条 QtCore 路径。  
**原因**：App 里链接到的是 brew Qt，同时 homebrew 全局 Qt 也被 DYLD 加载。  
**解决**：macdeployqt 将所有 Qt frameworks 和 plugins 复制到 .app 包内并修正 install_name。

#### ❌ 坑 10：第三方 dylib 签名冲突
**现象**：`libbrotlicommon.1.dylib` 等 homebrew 自带签名，macdeployqt 后签名验证失败，应用崩溃。  
**解决**：deploy_mac.sh 中先 `codesign --remove-signature *.dylib` 全去掉旧签名，再 `codesign --force --deep -s - app.app` 统一 ad-hoc 重签。

#### ❌ 坑 11：com.apple.quarantine 隔离属性
**现象**：双击打开提示"无法验证开发者"。  
**解决**：`xattr -dr com.apple.quarantine app.app`。deploy_mac.sh 自动执行。

### 9.5 数据导入相关

#### ❌ 坑 12：SQLite ATTACH 到 DuckDB 类型错乱
**现象**：DECIMAL 金额读成奇怪的整数。  
**解决**：放弃 ATTACH，用 Qt QSqlQuery 从 SQLite 读 → 手拼 INSERT → DuckDB。速度可以接受（规则数据量小）。

#### ❌ 坑 13：COPY 导出默认无表头
**解决**：导出 SQL 一律加 `HEADER TRUE`。

### 9.6 授权系统相关（2026-07-25 集中修复 6 项）

#### ❌ 坑 14：试用版授权不落盘 → 无限续期漏洞
**现象**：首次启动无 license.dat 时仅在内存设置 expire=30 天未写文件，用户删缓存/重启重复获取试用。  
**修复**：无 license.dat 时立即生成 Trial 授权码（带 HMAC 签名）写入 license.dat，重启后验证签名合法不重发。

#### ❌ 坑 15：时间篡改扣减有效期不持久化
**现象**：时间回拨扣减 expire_date 只在内存，SaveLicense 只写回原 license_key，重启后扣减失效恢复。  
**修复**：time_record.dat 新增第 4 段 `adjusted_expire` 保存扣减后的到期日；非永久版激活新授权时若已有扣减且更紧，保留扣减值。

#### ❌ 坑 16：单文件篡改记录可清除
**现象**：time_record.dat 存篡改标记，用户手动删除该文件即清零篡改痕迹。  
**修复**：双文件交叉校验，篡改时同时写入 `license.dat TAMPER=1` + `time_record.dat tampered=1`；删 time_record 后读 license.dat 的 TAMPER=1 仍判篡改并重建。

#### ❌ 坑 17：多网卡/虚拟网卡导致机器码跳变 → 激活失效
**现象**：机器码算法遍历所有网络接口拼 MAC，VPN/TAP/网桥等虚拟网卡增删导致机器码变化，授权码不匹配。  
**修复**：仅取第一张 Ethernet/WiFi 稳定网卡（跳过 Loopback/Docker/Bridge/TUN/TAP/空 MAC）；四维硬件组合（主板+CPU+盘+MAC）整体 MD5，任一维稳定即保持机器码稳定。

#### ❌ 坑 18：无效授权 DaysRemaining 返回 0 → 误报即将到期
**现象**：无效授权/过期授权 DaysRemaining 返回 0，IsNearExpiry(≤7) 误弹"还有 0 天到期"提示。  
**修复**：无效授权 DaysRemaining 返回 -1；IsNearExpiry 仅对 valid=true 且 remaining≥0 时返回 true。

#### ❌ 坑 19：机器码算法变更不同步授权生成器
**现象**：主程序升级四维机器码算法后，发行商仍用旧算法生成的授权码全部不匹配。  
**修复**：机器码/授权算法变更后**强制同步重新编译打包 license_generator.app**，与主程序同源；文档明确提醒旧算法授权码新版无法使用，老客户需重新采集机器码。

### 9.7 拉均重（平均重量合同定价）相关（2026-07-27 集中修复 6 项）

#### ❌ 坑 20：订单 CSV「客户编号」列填中文名称 → LEFT JOIN 匹配不到客户 → 诊断_合同ID 全空

**真实场景数据**：
- customers 主键 `customer_id = cust_1785106503_8611`，显示名 `customer_name = 珀莱雅`，客户级绑定 `avg_weight_tpl_id = contract_proya_25q2`（已正确设置）
- 用户 Excel/CSV 实际填的是「珀莱雅」（中文显示名），不是内部 UUID 式主键

**现象**：`LEFT JOIN customers c ON c.customer_id = i.customer_id` 永远匹配不上 → `cust_avg_weight_tpl_id`/`template_id` 全取默认值 → 拉均重完全不生效，且看起来是"用户没配置"。

**修复（2 处对称）**：
1. [calc_service.cpp L1030](file:///Users/cxd/duckdb/xiaoqiao_freight/src/services/calc_service.cpp#L1030) DuckDB 批量计算 SQL：
   ```sql
   LEFT JOIN customers c ON (
     TRIM(c.customer_id) = TRIM(i.customer_id)
     OR TRIM(c.customer_name) = TRIM(i.customer_id)
   )
   ```
2. [sqlite_rule_repository.cpp L1242-L1255](file:///Users/cxd/duckdb/xiaoqiao_freight/src/db/sqlite_rule_repository.cpp#L1242-L1255) `GetCustomer()` 单查（单条计算/前端查询）：主键查不到 → fallback 按 `TRIM(customer_name)=?` 查。

**教训**：凡是用户可见的输入列，**必须同时匹配主键和显示名**，不能假设用户会填内部 UUID。

#### ❌ 坑 21：方案A `avg_weight_zone_tpl_groups.template_id` 参与了资格判断 → 多个客户共用合同或分区来源不同 → 进池资格被误杀

**背景（数据模型设计）**：
- `avg_weight_zone_tpl_groups.avg_tpl_id`：**真正的绑定列**（哪份拉均重合同）
- `avg_weight_zone_tpl_groups.template_id`：**UI 展示列**（这个分区组来自哪张运费模板，仅用于前端勾选对话框里标"来源"，不参与业务逻辑）

**现象**：真实数据里，珀莱雅的 `contract_proya_25q2` 在「拉均重合同编辑」里勾选了 zone1，但该 zone1 分区行写入 SQLite 时 `template_id = cust_cust_1784852606_9205`（来自客户「111」的模板）≠ 珀莱雅订单实际归属的 `cust_cust_1785106503_8611`。

**根因 SQL（写反了，多余一层过滤）**：
```sql
-- 原错误逻辑：多加了 gt.template_id ∈ {awj.template_id,'*',''} 的硬匹配
EXISTS (SELECT 1 FROM avg_weight_zone_tpl_groups gt
  WHERE gt.avg_tpl_id = 合同ID
    AND (gt.template_id = 订单模板 OR gt.template_id='*' OR gt.template_id='')
    AND gt.group_code = 分区code)
```
→ `template_id` 不相等 → EXISTS = FALSE → 全省被排除 → 8 条订单「是否命中拉均重池 = 否」。

**修复（calc_service.cpp L825-L847）**：把分区资格判断改回**只按 avg_tpl_id + group_code 两列匹配**，完全去掉 gt.template_id / ex.template_id 的过滤（同理 avg_weight_zone_excludes.template_id 也去掉）：
```sql
EXISTS (SELECT 1 FROM avg_weight_zone_tpl_groups gt
         WHERE gt.avg_tpl_id = awj.lajz_avg_tpl_id
           AND gt.group_code = awj.group_code)
```

**教训**：一张表的字段要么"参与过滤/命中判断"，要么"仅 UI 展示"，不能混用，否则跨客户共用合同、跨模板导入分区时必然被多余条件误杀。需要文档中**强约束** UI 展示列绝不进 WHERE 子句。

#### ❌ 坑 22：T7 写的合同没有清理，和 T8 新合同同 version 同 template_id → DuckDB `GROUP BY MAX(version)` 随机取一份

**现象**：billing_params_test 测试 T8（方案A分区排除上海）结果有时是对的，有时完全不生效，同一份代码两个分支随机切换，看起来是「DuckDB 非确定性」。

**根因**：T7 先写入 `POLAIYA_LAJZ_001`，T7.1 又写入 `UT_LAJZ_DIRECT_001`，T8 再写 `POL_TPL_001`，三份都是 `template_id='tpl_a'`、`version=1`、`is_active=1`。avg_weight_active 的取最大 version 逻辑里，同 version 多份 → DuckDB 非确定取 → T8 随机拿到前两份之一（POLAIYA_LAJZ_001 是方案B、min_tickets=5，T8 期望 POL_TPL_001 方案A min=3）。

**修复（billing_params_test T8 开头 + 真实使用建议）**：
- T8 测试开头先循环清理旧合同：从 `avg_weight_templates / zones / zone_tpl_groups / zone_excludes` 四张表 DELETE 掉 UT_LAJZ_DIRECT_001 / POLAIYA_LAJZ_001。
- 真实生产建议：升级旧合同一律 version++，不要和同 template_id 老合同同 version。

#### ❌ 坑 23：`CalcBatch()` 入口没 `ReloadRules()` → 前端 UI 点了保存 → 后台 DuckDB 还是老分区/老排除省 → "保存了但计算不生效"

**修复**：[calc_service.cpp CalcBatch 入口](file:///Users/cxd/duckdb/xiaoqiao_freight/src/services/calc_service.cpp) 加防御式 `repo.ReloadRules(duckdb_)`，和 CalcFromFile 行为一致，确保 UI 保存 ↔ 批量计算无感知延迟。

#### ❌ 坑 24：批量计算 .app 打包后规则库路径不是 `~/.xiaoqiao_freight/rules.db`，默认落到 QStandardPaths::AppDataLocation

**真实路径（macOS）**：
```
/Users/<用户名>/Library/Application Support/杭州喵喵至家网络有限公司/小乔运费结算/rules.db
```
直接排障用 `sqlite3` CLI 打开这个路径，不要去看测试工具的 `billing_params_test/rules.db`。

---

## 10. 部署与打包

### 10.1 编译（macOS）

前置依赖：Qt6 6.5+、CMake 3.20+、duckdb 1.5.4 库和头文件。

```bash
cd xiaoqiao_freight
mkdir build && cd build
cmake .. -DCMAKE_PREFIX_PATH=/opt/homebrew/opt/qt6
make -j$(sysctl -n hw.ncpu)
```

编译产物位置：
```
build/bin/
├── xiaoqiao_freight.app       # 主程序（含自动部署）
└── license_generator.app      # 授权生成器（含自动部署）
```

### 10.2 自动部署（CMake POST_BUILD）

CMakeLists.txt 中 `APPLE` 块配置了 POST_BUILD 自定义命令，`make` 完成后自动：
```bash
bash scripts/deploy_mac.sh <target_bundle_dir>
```

### 10.3 deploy_mac.sh 流程详解

```bash
1. 找到 macdeployqt（默认 /opt/homebrew/bin/macdeployqt）
2. 执行 macdeployqt <app.app> -verbose=1
   → 拷贝 Qt Frameworks、Plugins、修正 install_name
3. 移除所有第三方 dylib 的旧签名：
   find Frameworks PlugIns -name "*.dylib" -exec codesign --remove-signature {} \;
4. 整个 .app 包 ad-hoc 重签：
   codesign --force --deep -s - <app.app>
5. 清除隔离属性：
   xattr -dr com.apple.quarantine <app.app>
```

### 10.4 手动重新部署

```bash
bash xiaoqiao_freight/scripts/deploy_mac.sh build/bin/xiaoqiao_freight.app
bash xiaoqiao_freight/scripts/deploy_mac.sh build/bin/license_generator.app
```

### 10.5 制作 DMG

使用 `hdiutil create` 或第三方工具（如 create-dmg）制作拖拽安装 DMG。已生成产品：`build/xiaoqiao_freight.dmg`（约 55MB）。

### 10.6 版本号管理（CMake 单源真源 v1.00 ~ ）

> **唯一一处真源**：`CMakeLists.txt` 第一行 `project(xiaoqiao_freight VERSION X.Y.Z LANGUAGES CXX)`。修改这里会**自动传播**到：
> - Info.plist 里的 `CFBundleShortVersionString / CFBundleVersion`（macOS Finder/Get Info 显示）
> - GUI「关于」页显示的 `APP_VERSION_DISPLAY`（格式 `X.YZ`，比如 1.0.9 → 1.09）
> - 所有输出诊断列 / 日志里埋点版本

| 发布版本 | CMake VERSION | GUI 显示 | 日期 | 核心变更 |
|---|---|---|---|---|
| v1.00 | 1.0.0 | 1.00 | 2026-07-20 | 项目初版：单条/批量计算、表头自动映射、6 级阶梯+燃油/地区/自定义策略、SQLite 规则库 |
| v1.01 | 1.0.1 | 1.01 | 2026-07-21 | 客户管理 + 客户级默认模板绑定 + 折扣率 |
| v1.02 | 1.0.2 | 1.02 | 2026-07-22 | 历史记录服务、Diff 对比分析、旧数据清理 |
| v1.03 | 1.0.3 | 1.03 | 2026-07-23 | 自动性能调优（90% 内存/CPU）、DuckDB 列式参数优化 |
| v1.04 | 1.0.4 | 1.04 | 2026-07-24 | 8 家快递模板指纹识别 + 管理对话框（自定义覆盖内置） |
| v1.05 | 1.0.5 | 1.05 | 2026-07-24 | 图标系统全套升级为「祥云快递盒」方案B + icns/9档PNG/QRC 一键生成 |
| v1.06 | 1.0.6 | 1.06 | 2026-07-25 | 授权系统6项大修：试用版落盘/时间扣减持久化/双文件交叉防篡改/稳定MAC四维机器码/无效授权 days=-1 |
| v1.07 | 1.0.7 | 1.07 | 2026-07-26 | 拉均重合同第一版：双绑定(客户级/模板级)、方案A分区复用 + 排除省、方案B自定义省白名单、4 列诊断、开关控制 |
| v1.08 | 1.0.8 | 1.08 | 2026-07-27 | 修复方案A进池资格 EXISTS 写反 + CalcBatch 入口强制 ReloadRules + billing_params_test T7/T7.1/T8 覆盖 |
| v1.09 | 1.0.9 | 1.09 | 2026-07-27 | 拉均重真实场景打通：LEFT JOIN customers 同时匹配 customer_name；方案A去掉 template_id 多余过滤（展示列不进 WHERE） |

**升级流程（推荐）**：
```bash
cd xiaoqiao_freight
# 1. 改 VERSION
perl -i -pe 's/project\(xiaoqiao_freight VERSION \d+\.\d+\.\d+/project(xiaoqiao_freight VERSION 1.0.10/' CMakeLists.txt
# 2. 重新跑单元测试
cd build && cmake .. && make -j8 billing_params_test && ./bin/billing_params_test | grep FAIL
# 3. 打包
make -j8 xiaoqiao_freight
# 4. 交付产物：build/bin/xiaoqiao_freight_v1.10.app
```

---

## 11. 授权生成器工具

**源码**：`tools/license_generator/`，CMake 中作为第二目标构建 `license_generator.app`。

### 界面

```
┌─────────────────────────────────────────────┐
│ 机器码 [用户粘贴的客户机器码 XXXX-...]       │
│ 授权类型 [▼ 试用版/个人版/企业版/永久版]      │
│  天数  [  N ]  ← 非永久版显示                │
│ 到期日  [2026-12-31 23:59:59]  ← 非永久      │
│ 密钥    [XiaoQiaoFreight2026@#$SecretKey888] │
│                                            │
│         [生成授权码]  [复制结果]             │
│                                            │
│ ╔═══════════════════════════════════════╗    │
│ ║ 生成的授权码（带每6字符横杠分隔）       ║    │
│ ╚═══════════════════════════════════════╝    │
│                    [ 验证结果 ]              │
└─────────────────────────────────────────────┘
```

### 主要逻辑

**OnGenerate()**：
```
读取 机器码 + 类型 + 到期日/天数 + 密钥
→ LicenseManager::GenerateLicenseKey(mc, type, exp, secret)
→ 输出授权码到文本框
```

**OnValidate()**：
```
粘贴授权码 + 机器码 + 密钥
→ LicenseManager::VerifyLicenseKey(key, mc, secret)
→ 显示类型 / 到期日 / 是否有效 / 错误信息
```

> ⚠️ **该工具仅供发行商内部使用，密钥泄漏会导致授权被伪造**。

---

## 12. 附录：关键代码位置速查

| 功能 | 文件 | 函数/符号 |
|------|------|----------|
| 应用启动流程 | [main.cpp](file:///Users/cxd/duckdb/xiaoqiao_freight/src/main.cpp#L12-L62) | `main()` |
| 自动性能调优 | [app_config.hpp](file:///Users/cxd/duckdb/xiaoqiao_freight/src/core/app_config.hpp#L90-L95) | `ApplyAutoPerformance()` |
| 系统内存/CPU 采集 | [app_config.hpp](file:///Users/cxd/duckdb/xiaoqiao_freight/src/core/app_config.hpp#L49-L64) | `GetTotalMemoryMB()/GetCpuCoreCount()` |
| 数据结构定义 | [freight_types.hpp](file:///Users/cxd/duckdb/xiaoqiao_freight/src/core/freight_types.hpp) | `StrategyScope/Type, SurchargeStrategy, CalcResult` |
| 四维机器码生成(主板+CPU+盘+网卡，跨平台) | [license_manager.cpp](file:///Users/cxd/duckdb/xiaoqiao_freight/src/core/license_manager.cpp#L37-L180) | `GenerateMachineCode() + ReadBoardId/ReadCpuId/ReadDiskUuid/ReadFirstStableMac()` |
| 试用版授权自动落盘(防无限续期) | [license_manager.cpp](file:///Users/cxd/duckdb/xiaoqiao_freight/src/core/license_manager.cpp) | `LoadLicense() 无文件时 CreateAndSaveTrialLicense()` |
| 授权码生成 | [license_manager.cpp](file:///Users/cxd/duckdb/xiaoqiao_freight/src/core/license_manager.cpp) | `GenerateLicenseKey()` |
| 授权码验证 | [license_manager.cpp](file:///Users/cxd/duckdb/xiaoqiao_freight/src/core/license_manager.cpp) | `VerifyLicenseKey() + LoadLicense() 多行解析(授权码+TAMPER=1)` |
| 双文件防时间篡改+有效期扣减持久化 | [license_manager.cpp](file:///Users/cxd/duckdb/xiaoqiao_freight/src/core/license_manager.cpp) | `CheckTimeTampering() + WriteTimeRecord(四段) + SaveLicense(保留TAMPER行)` |
| 无效授权 DaysRemaining=-1 防误报 | [license_manager.cpp](file:///Users/cxd/duckdb/xiaoqiao_freight/src/core/license_manager.cpp) | `DaysRemaining() + IsNearExpiry(valid&&remaining>=0)` |
| DuckDB 初始化+性能配置 | [duckdb_manager.cpp](file:///Users/cxd/duckdb/xiaoqiao_freight/src/db/duckdb_manager.cpp#L17-L50) | `DuckDBManager::Init()` |
| 从 SQLite 同步 12 张规则表到 DuckDB | [duckdb_manager.cpp](file:///Users/cxd/duckdb/xiaoqiao_freight/src/db/duckdb_manager.cpp#L52-L247) | `LoadRulesFromSQLite()` |
| 规则库 schema 版本升级(v10→v11) | [sqlite_rule_repository.cpp](file:///Users/cxd/duckdb/xiaoqiao_freight/src/db/sqlite_rule_repository.cpp#L38-L94) | `SqliteRuleRepository::Init()` |
| 默认中通模板(6分区×6阶梯) | [sqlite_rule_repository.cpp](file:///Users/cxd/duckdb/xiaoqiao_freight/src/db/sqlite_rule_repository.cpp#L228-L328) | `CreateDefaultTemplate()` |
| 6 个重量阶梯定义列表 | [sqlite_rule_repository.cpp](file:///Users/cxd/duckdb/xiaoqiao_freight/src/db/sqlite_rule_repository.cpp#L299-L306) | `tiers` 局部变量 |
| 6 个分区定义 | [sqlite_rule_repository.cpp](file:///Users/cxd/duckdb/xiaoqiao_freight/src/db/sqlite_rule_repository.cpp#L263-L276) | `zones` 局部变量 |
| 新增客户时自动创建专属报价 | [sqlite_rule_repository.cpp](file:///Users/cxd/duckdb/xiaoqiao_freight/src/db/sqlite_rule_repository.cpp#L718-L820) | `AddCustomer()` |
| 单条运费计算 SQL(global/template scope 忽略模板绑定) | [calc_service.cpp](file:///Users/cxd/duckdb/xiaoqiao_freight/src/services/calc_service.cpp#L130-L160) | `CalcSingle() strategy_surcharge_calc 条件A` |
| 10 层 CTE 批量计算 SQL(global/template scope+per_volume CASE) | [calc_service.cpp](file:///Users/cxd/duckdb/xiaoqiao_freight/src/services/calc_service.cpp#L410-L450) | `BuildCalcSQL() strategy_surcharge_calc WHERE 条件A/B/C` |
| 7 标准列(含 shop_id)+两轮表头自动映射(精确优先+子串防误伤) | [calc_service.cpp](file:///Users/cxd/duckdb/xiaoqiao_freight/src/services/calc_service.cpp#L450-L510) | `AutoMapColumns() + shop_id 映射 + customer_id 空用 shop_id 兜底` |
| TRY_CAST + COALESCE 创建标准化表(含 shop_id 列) | [calc_service.cpp](file:///Users/cxd/duckdb/xiaoqiao_freight/src/services/calc_service.cpp#L515-L545) | `CreateNormalizedTable(_input_normalized 含 shop_id)` |
| 规则设置-加价策略 UI(per_volume下拉+移除模板级scope+金额格式) | [rule_setting_dialog.cpp](file:///Users/cxd/duckdb/xiaoqiao_freight/src/ui/dialogs/rule_setting_dialog.cpp#L468-L575) | `LoadSurchargeTable() 显示 + ShowSurchargeDialog() 增改` |
| 规则设置-燃油/地区/加价 顶部模板筛选下拉 | [rule_setting_dialog.cpp](file:///Users/cxd/duckdb/xiaoqiao_freight/src/ui/dialogs/rule_setting_dialog.cpp) | 三个 Tab 的顶部 ComboBox 模板筛选 |
| 历史记录 CRUD | [history_service.cpp](file:///Users/cxd/duckdb/xiaoqiao_freight/src/services/history_service.cpp) | `AddHistory/QueryHistory/DeleteHistory/CleanupOldData` |
| 主窗口布局 + QSS 样式 | [main_window.cpp](file:///Users/cxd/duckdb/xiaoqiao_freight/src/ui/main_window.cpp#L31-L229) | `SetupUI()/SetupStyles()` |
| 页脚官网链接（外链+下划线） | [main_window.cpp](file:///Users/cxd/duckdb/xiaoqiao_freight/src/ui/main_window.cpp#L133-L142) | footer_label_ |
| 授权启动后检查弹窗 | [main_window.cpp](file:///Users/cxd/duckdb/xiaoqiao_freight/src/ui/main_window.cpp#L300-L313) | `CheckLicenseStartup()` |
| macdeployqt + 签名修复脚本 | [deploy_mac.sh](file:///Users/cxd/duckdb/xiaoqiao_freight/scripts/deploy_mac.sh) | 第 1-46 行 |
| T7 模板指纹数据结构+8内置模板 | [app_config.hpp](file:///Users/cxd/duckdb/xiaoqiao_freight/src/core/app_config.hpp#L125-L150) | `TemplateFingerprint + BuildBuiltinTemplates()` |
| T7 识别+关键词+列名双重评分 +自定义优先覆盖去重 | [template_recognizer.cpp](file:///Users/cxd/duckdb/xiaoqiao_freight/src/services/template_recognizer.cpp#L40-L120) | `RecognizeFromColumns()` |
| T7 模板管理窗口(列表+启用+增改删+信号) | [courier_template_manager_dialog.cpp](file:///Users/cxd/duckdb/xiaoqiao_freight/src/ui/dialogs/courier_template_manager_dialog.cpp#L1-L320) | `OnAddTemplate()/OnEditTemplate()/OnDeleteTemplate()/OnToggleEnabled()` |
| T7 模板编辑对话框(关键词自动分割+列映射表校验) | [courier_template_edit_dialog.cpp](file:///Users/cxd/duckdb/xiaoqiao_freight/src/ui/dialogs/courier_template_edit_dialog.cpp#L1-L280) | `OnOk()` + 校验逻辑 |
| 批量计算页「⚙️ 管理模板」入口按钮 | [batch_calc_dialog.cpp](file:///Users/cxd/duckdb/xiaoqiao_freight/src/ui/dialogs/batch_calc_dialog.cpp#L880-L920) | `OnManageTemplates()` |
| 方案B「祥云快递盒」代码绘制Logo（窗口+关于页+菜单） | [icon_manager.cpp](file:///Users/cxd/duckdb/xiaoqiao_freight/src/ui/icon_manager.cpp#L294-L407) | `DrawSchemeBLogo() / GenerateLogoIcon()` |
| Mac App Bundle 注入 .icns + Info.plist CFBundleIconFile | [CMakeLists.txt](file:///Users/cxd/duckdb/xiaoqiao_freight/CMakeLists.txt#L96-L109) | `MACOSX_BUNDLE_ICON_FILE + target_sources(.icns)` |
| 一键生成 .icns + 9档PNG 资源工具 | [gen_icon/main.cpp](file:///Users/cxd/duckdb/xiaoqiao_freight/tools/gen_icon/main.cpp#L1-L240) | `DrawLogoSchemeB() + iconutil -c icns` |
| T7 Headless 5项接口回归测试 | [t7_test/main.cpp](file:///Users/cxd/duckdb/xiaoqiao_freight/tools/t7_test/main.cpp#L1-L180) | 1)8内置 2)开关 3)自定义覆盖 4)CRUD 5)持久化 |
| 全局 Alt-Tab 窗口统一图标 app.setWindowIcon | [main.cpp](file:///Users/cxd/duckdb/xiaoqiao_freight/src/main.cpp#L50-L52) | `app.setWindowIcon(icons.GetIcon(app_logo, LOGO, 64))` |
| 拉均重合同双绑定查找（客户级优先 + 模板级 fallback MAX(version)） | [calc_service.cpp](file:///Users/cxd/duckdb/xiaoqiao_freight/src/services/calc_service.cpp#L717-L814) | `avg_weight_active CTE + avg_weight_joined CTE COALESCE(客户级子查询, 模板级子查询)` |
| 拉均重方案A进池资格（分区勾选+排除省） | [calc_service.cpp](file:///Users/cxd/duckdb/xiaoqiao_freight/src/services/calc_service.cpp#L825-L847) | `NOT EXISTS(未配置分区) OR EXISTS(匹配分区) AND NOT EXISTS(排除省)` |
| 拉均重方案B进池资格（自定义省白名单） | [calc_service.cpp](file:///Users/cxd/duckdb/xiaoqiao_freight/src/services/calc_service.cpp#L848-L858) | `EXISTS(avg_weight_zones 匹配省)` |
| 客户 LEFT JOIN（同时匹配 customer_id 和 customer_name） | [calc_service.cpp](file:///Users/cxd/duckdb/xiaoqiao_freight/src/services/calc_service.cpp#L1029-L1031) | `TRIM(customer_id)=TRIM(i.customer_id) OR TRIM(customer_name)=TRIM(i.customer_id)` |
| GetCustomer 两阶段查找（主键 + 中文名称 fallback） | [sqlite_rule_repository.cpp](file:///Users/cxd/duckdb/xiaoqiao_freight/src/db/sqlite_rule_repository.cpp#L1242-L1267) | `先 WHERE customer_id=? → 失败再 WHERE TRIM(customer_name)=?` |
| 拉均重 4 张表写库（含失败重试+详细诊断日志） | [sqlite_rule_repository.cpp](file:///Users/cxd/duckdb/xiaoqiao_freight/src/db/sqlite_rule_repository.cpp#L1480-L1580) | `SaveAvgWeightTemplate / SetAvgWeightTplGroups / SetAvgWeightExcludes / SetAvgWeightZones` |
| 拉均重合同编辑 UI（方案A/B切换 + 分区勾选左右双栏 + 排除省） | [rule_setting_dialog.cpp](file:///Users/cxd/duckdb/xiaoqiao_freight/src/ui/dialogs/rule_setting_dialog.cpp#L2540-L2800) | `rb_plan_a / sel_tpl_groups / excl_tplg_provs` |
| billing_params_test T7（方案B端到端 10 行江浙沪 + X1 超上限） | [billing_params_test/main.cpp](file:///Users/cxd/duckdb/xiaoqiao_freight/tools/billing_params_test/main.cpp#L390-L490) | `POLAIYA_LAJZ_001` 合同 + L1-L10 应全部采用拉价 |
| billing_params_test T7.1（SqliteRuleRepository 层 CRUD 直写 + 回读） | [billing_params_test/main.cpp](file:///Users/cxd/duckdb/xiaoqiao_freight/tools/billing_params_test/main.cpp#L492-L556) | `SaveAvgWeightTemplate + SetAvgWeightTplGroups/Excludes/Zones` 4 子操作 |
| billing_params_test T8（方案A + 模板级绑定 + Z1勾选 + Z1排除上海） | [billing_params_test/main.cpp](file:///Users/cxd/duckdb/xiaoqiao_freight/tools/billing_params_test/main.cpp#L559-L720) | `POL_TPL_001` + CalcFromFile 真实 CSV 工作流 |
| 拉均重开关双门控（C++端 + SQL __lajz_global CTE） 关时占位列全空 | [calc_service.cpp](file:///Users/cxd/duckdb/xiaoqiao_freight/src/services/calc_service.cpp#L706-L712 + L950-L980) | `enable_avg_weight ? 真实CTE : 占位CTE` |
| CMake 单源版本号（VERSION 一处改动 三处输出） | [CMakeLists.txt](file:///Users/cxd/duckdb/xiaoqiao_freight/CMakeLists.txt#L1-L100) | `project(VERSION X.Y.Z) + configure_file(version_defs.hpp.in)` |

---

*文档版本：1.3*  
*最后更新：2026-07-27（v1.3 更新要点：拉均重真实场景 6 坑、版本号管理 v1.00~v1.09、§12 附录加拉均重核心代码位置速查）*  
*版权所有 © 2026 杭州喵喵至家网络有限公司 www.hbdxm.com*
