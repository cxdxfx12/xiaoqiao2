# 小乔运费结算系统 - 项目文档 v1.1a

**文档版本号**：v1.1a（2026-07-24 修订版）  
**修订说明**：  
  ① §4.4.5–4.4.7 新增「v1.1a 用户可自定义顿号关键字 + 中文红星标准列 + 记住映射」；  
  ② §13.1 新增 v1.1a 变更日志（U1~U6 六项体验升级）；  
  ③ §16 从原来 1 页链接文档扩展为完整的三方案详解 + 验证脚本 + 5 条 DuckDB SET 快速兜底。  
  （其他章节与 v1.1 正式版保持一致，未改动。）

## 目录

1. [项目概述](#1-项目概述)
2. [技术栈](#2-技术栈)
3. [项目结构](#3-项目结构)
4. [核心模块详解](#4-核心模块详解)
   - 4.1 [计算引擎](#41-计算引擎)
   - 4.2 [规则系统](#42-规则系统)
   - 4.3 [数据存储层](#43-数据存储层)
   - 4.4 [表头映射机制](#44-表头映射机制)
     - 4.4.5 关键字配置的持久化与合并策略（v1.1a UX 重构后）
     - 4.4.6 v1.1a 用户体验：6 行顿号表 + 编辑弹窗
     - 4.4.7 「记住映射」回写机制（批量计算对话框）
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
13. [版本变更日志 v1.1（Bug 修复总览）](#13-版本变更日志-v11bug-修复总览)
    - 13.1 v1.1a 功能增强：表头关键字 UX 重设计
14. [批处理命令行工具 batch_runner](#14-批处理命令行工具-batch_runner)
15. [性能实测基线（真实 260 万行帐单）](#15-性能实测基线真实-260-万行帐单)
16. [性能优化路线图（完整方案 + 实施步骤）](#16-性能优化路线图完整方案--实施步骤)
    - 16.0 快速决策表
    - 16.1 方案一：Parquet 热缓存 + 并行 xlsx Reader
    - 16.2 方案二：子查询 → 预聚合 CTE + LATERAL JOIN
    - 16.3 方案三：列式 Parquet 主路径 + xlsx 异步转码
    - 16.4 统一验证脚本（每次改完跑一次）
    - 16.5 快速兜底：DuckDB SET 不改代码就能 +15%

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
| 关于/授权 | 软件版本信息、机器码生成、授权码激活、授权有效期管理 |

### 核心特性

- ⚡ **高性能**：基于 DuckDB 列式存储引擎，百万级数据秒级计算
- 📊 **批量处理**：支持 Excel/CSV/Parquet 多种格式导入导出
- 🎯 **智能表头映射**：自动识别中英文表头（精确匹配优先 + 子串匹配兜底），缺失必填列时弹手动映射对话框
- 🔧 **灵活规则**：6 级重量阶梯、燃油附加费、地区加价（省/市/区三级）、自定义策略
- 🖥️ **自动性能调优**：启动时自动检测系统资源，使用 90% 内存和 CPU 核心数配置 DuckDB
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
├── CMakeLists.txt              # CMake 构建配置（3 个目标：主程序 GUI + 授权生成器 GUI + 批处理命令行）
├── PROJECT.md                  # 项目文档 v1.0
├── PROJECT_v1.1.md             # 项目文档 v1.1（本文档）
├── PROJECT_v1.1_new.md         # v1.1 原始草稿
├── PERFORMANCE_OPTIMIZATION.md # 性能优化路线图（§16 详细展开）
├── .gitignore                  # Git 忽略规则
├── resources/
│   └── resources.qrc           # Qt 资源文件（预留图标等资源）
├── scripts/
│   └── deploy_mac.sh           # macOS 部署脚本（macdeployqt + 签名修复 + 去隔离属性）
├── tools/
│   ├── license_generator/      # 授权生成器工具（独立 GUI 程序）
│   │   ├── main.cpp
│   │   ├── license_generator_widget.hpp
│   │   └── license_generator_widget.cpp
│   └── batch_runner/           # 命令行批量运费计算工具（无 GUI，脚本化集成用）
│       └── main.cpp
└── src/
    ├── main.cpp                # 程序入口
    ├── core/                   # 核心类型与配置
    │   ├── app_config.hpp      # 应用配置（单例、性能调优、路径管理）
    │   ├── app_config.cpp
    │   ├── freight_types.hpp   # 数据结构定义（枚举、CalcResult、SurchargeStrategy）
    │   ├── license_manager.hpp # 授权管理器（单例）
    │   └── license_manager.cpp
    ├── db/                     # 数据存储层
    │   ├── sqlite_rule_repository.hpp  # SQLite 规则库仓储
    │   ├── sqlite_rule_repository.cpp
    │   ├── duckdb_manager.hpp         # DuckDB 管理器（单例）
    │   └── duckdb_manager.cpp
    ├── services/               # 业务服务层
    │   ├── calc_service.hpp           # 计算服务（单条/批量/文件，带 ProgressChanged/CalcFinished signal）
    │   ├── calc_service.cpp
    │   ├── rule_service.hpp           # 规则服务（转发至 Repository，发 RulesChanged 信号）
    │   ├── rule_service.cpp
    │   ├── history_service.hpp        # 历史记录服务（增删查改、清理）
    │   └── history_service.cpp
    └── ui/                     # 界面层
        ├── main_window.hpp     # 主窗口
        ├── main_window.cpp
        ├── icon_manager.hpp    # 图标管理器（程序化绘制，缓存）
        ├── icon_manager.cpp
        └── dialogs/            # 对话框
            ├── single_calc_dialog.*     # 单条计算
            ├── batch_calc_dialog.*      # 批量计算（后台异步线程 + 进度动画）
            ├── compare_dialog.*         # 对比分析
            ├── history_dialog.*         # 历史记录
            ├── rule_setting_dialog.*    # 规则设置（多 Tab）
            ├── template_edit_dialog.*   # 模板编辑（阶梯价、分区、燃油）
            ├── customer_setting_dialog.*# 客户设置 + 客户报价矩阵
            ├── system_setting_dialog.*  # 系统设置（性能）
            ├── header_mapping_dialog.*  # 表头手动映射
            └── about_dialog.*           # 关于 + 授权激活
```

### 架构设计（三层架构）

```
┌──────────────────────────────────────────────┐
│              UI 层 (Qt Widgets)               │
│  MainWindow / 10 个 Dialog / IconManager      │
└──────────────────────┬───────────────────────┘
                       ↓
┌──────────────────────────────────────────────┐
│           Service 层 (业务逻辑)               │
│  CalcService  RuleService  HistoryService     │
└──────────────────────┬───────────────────────┘
                       ↓
┌──────────────────────────────────────────────┐
│           DB 层 (数据持久化)                   │
│  SQLite (rules.db + history.db)               │
│  DuckDB (calc.duckdb - 列式计算引擎)          │
└──────────────────────────────────────────────┘
```

**命名空间约定**：所有代码位于 `freight::xxx` 命名空间下
- `freight::core` — 核心配置与类型
- `freight::db` — 数据存储层
- `freight::services` — 业务服务层
- `freight::ui` — 界面层（含 `freight::ui::dialogs` 子命名空间）
- `freight::tools` — 工具模块（授权生成器）

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
    --    SUM(CASE strategy_type: fixed / percentage / per_weight / per_volume)
    --    作用范围：
    --      a) global：对所有订单
    --      b) province：LEFT JOIN surcharge_provinces 按省份命中
    --      c) customer：LEFT JOIN surcharge_customers 按 customer_id 命中
    --      d) 日期生效：LEFT JOIN surcharge_date_ranges，当前日期在 [start_date, end_date] 内
    --    重量过滤：min_weight ≤ charge_weight ≤ max_weight
    --    必须 is_active = 1
)
SELECT
    "订单号","客户编号","目的省份","目的城市",
    "实际重量(KG)","体积重量(KG)","计费重量(KG)",
    "基础运费","燃油附加费","地区加价","其他附加费","总运费","币种"
FROM strategy_surcharge_calc
```

> **v1.1 修复点**：
> - 原 12 列表头补齐为 **13 列**（新增/拆分正确的"地区加价"和"其他附加费"两列，避免预览列错位）。
> - `strategy_surcharge_calc` 子查询已全面接入：`per_volume` 按体积重 / 计费重兜底金额、`surcharge_customers` 客户级绑定、`surcharge_date_ranges` 日期区间过滤。
> - `col_or_literal_str()` / `col_or_literal_num()` 工具函数自动在缺列时把空列名替换成 `' '` / `0` 字面量，保证 SQL 合法可执行。

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

| 值 | 计算公式 | v1.1 新增 |
|----|----------|:---------:|
| fixed | 固定 `amount` 元 | |
| percentage | `base_fee × amount` | |
| per_weight | `charge_weight × amount`（每公斤金额） | |
| per_volume | `COALESCE(NULLIF(vol_weight,0), charge_weight) × amount`（按体积重 / 缺省计费重） | ✅ |

**作用范围 (strategy_scope)**：

| 值 | 含义 | 关联表 | v1.1 新增 |
|----|------|--------|:---------:|
| global | 对所有订单生效 | - | |
| template | 对指定模板生效 | -（template_id 字段） | |
| province | 对指定省份生效 | surcharge_provinces | |
| customer | 对指定客户生效 | surcharge_customers | ✅ 正式接入计算 SQL |

**其他属性**：
- `priority`：优先级（目前仅排序，金额是 SUM 叠加）
- `min_weight / max_weight`：重量门槛（0 或 NULL 表示不限）
- `is_active`：启用开关
- `surcharge_date_ranges`：**v1.1 正式接入**，按 `start_date ≤ CURRENT_DATE ≤ end_date` 过滤（若存在多条日期区间记录，则策略命中所有"当前日期位于区间内"的行 SUM 叠加）

**内置默认策略 1 条**（由 `InitDefaultData()` 统一控制仅首次或 schema v10 之前执行，避免重复插入）：
1. `packing_fee`：包装服务费，全局固定 1 元

> **v1.1 修复点**：`InitDefaultData()` 由 `need_init_default = (current_version < 10) || IsFirstRun()` 统一判定保证只执行一次，**避免默认规则/阶梯/策略重复插入 → 运费被错误翻倍**（H1 严重 bug 修复）。

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
- `Init(db_path)` — 设置 `memory_limit`（系统内存 90%）、`threads`（CPU 核数 90%）；**excel 扩展采用 "先 LOAD → 失败再 INSTALL"**，避免每次启动重复下载（冷启动加速）
- `LoadRulesFromSQLite(path)` — 12 张表的 DROP→CREATE→INSERT 全流程
- `ReloadRules(path)` — 重载（规则变更后调用）
- `ImportFromFile(table, file)` — CSV/Parquet/XLSX → DuckDB 表
- `ExportToFile(table, file)` — DuckDB 表 → CSV/Parquet/XLSX（带表头）；.xls 后缀走 xlsx 兼容写出
- `GetRowCount(table)` / `TableExists(table)` — 元信息查询
- `CreateConnection()` — 为每次计算创建独立 `duckdb::Connection`

> **DuckDB 版本兼容 workaround + v1.1 可靠性修复**：
> 1. `MaterializedQueryResult::GetValue<T>(col, row)` 模板方法存在类型转换 bug（小数截断），统一改为 `res->GetValue(col, row).GetValue<T>()` 先拿 Value 对象再转换。
> 2. `TableExists()` 放弃 `information_schema.tables`，改用 `SHOW TABLES LIKE '<name>'`，失败再 `DESCRIBE SELECT * FROM t LIMIT 1` 兜底，彻底避免偶发"查不到已建表"的坑。
> 3. SQLite 同步到 DuckDB 时的 QVariant 类型处理：`switch(metaType.id)` 覆盖 Bool / Int / UInt / LongLong / ULongLong / Float / Double 全部数值形态，**防止 UInt/Bool/Float 被当作字符串插入到数值列导致金额错位**。
> 4. `QSqlDatabase::removeDatabase()` 不再函数末尾直接调用，改为 `QTimer::singleShot(0, ...)` 延迟到下一事件循环迭代再移除，**规避 Qt 著名的 "connection ... is still in use" 警告**（QSqlDriver 内部有活跃引用时不能直接移除）。同时连接名也改 `duckdb_import_1/2/...` + `rules_conn_1/2/...` 自增，Reload 多次也不会重名。
> 5. `.xls` 后缀的导出兼容性：COPY 一律按 `FORMAT xlsx` 写，保证 Office/WPS 直接双击打开无报错。
> 6. SQL 注入防护：从 Qt QSqlQuery 读出字符串值时统一做 `s.replace("'", "''")`，不再信任外部数据。

---

### 4.4 表头映射机制

**文件**：[calc_service.cpp](file:///Users/cxd/duckdb/xiaoqiao_freight/src/services/calc_service.cpp) 的 `AutoMapColumns()`、`NormalizeColumns()`、`CreateNormalizedTable()`

批量计算的文件输入表头千变万化，映射机制保证正确关联到标准列。

#### 标准列定义（6 列）

| 标准列 | 中文名 | 必填 | 映射失败兜底 |
|--------|--------|:----:|------------|
| order_id | 订单号 | 否 | '' |
| customer_id | 客户编号 | 否 | '' |
| dest_province | 目的省份 | ✅ 是 | 报错，弹手动映射对话框 |
| dest_city | 目的城市 | 否 | '' |
| weight | 实际重量 | ✅ 是 | 报错，弹手动映射对话框 |
| vol_weight | 体积重量 | 否 | 0 |

#### 映射关键字（每列多关键字，中英文混合）

| 标准列 | 关键字列表 |
|--------|-----------|
| order_id | order_id, order_no, waybill, 订单号, 订单编号, 运单号, 快递单号, 单号 |
| dest_province | dest_province, to_province, province, 目的省份, 省份, 收件省份, 到达省份, 收货省份 |
| dest_city | dest_city, to_city, city, 目的城市, 城市, 收件城市, 到达城市, 收货城市 |
| weight | weight, actual_weight, gross_weight, real_weight, 结算重量, 重量, 实际重量, 实重, 毛重, 计费重量 |
| vol_weight | vol_weight, volume_weight, volumetric_weight, 体积重量, 体积重, 体积, 抛重 |
| customer_id | customer_id, cust_id, customer, 客户id, 客户编号, 客户, 客户名称, 客户名 |

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

#### 4.4.5 关键字配置的持久化与合并策略（v1.1a UX 重构后）

v1.0 关键字硬编码在 `AutoMapColumns()` 里，不允许用户改。v1.1a 改造成**「系统默认 + 用户自定义叠加去重」双轨制**：

| 项目 | 说明 |
|------|------|
| **系统默认关键字**（内置 60+ 条）| 在 `AppConfig::BuildDefaultMappingKeywords()` 里静态生成，只读不改 |
| **用户自定义关键字** | 写入 `config.ini [mapping_keywords]` 段（QSettings beginWriteArray 格式，重启也生效）|
| **生效关键字 = 默认 + 自定义合并** | `AppConfig::GetEffectiveMappingKeywords()` 合并两套，按关键字大小写不敏感去重 |

持久化数据形态（`config.ini` 片段）：
```ini
[mapping_keywords]
size=3
1\standard=order_id
1\keyword=珀莱雅单号
2\standard=customer_id
2\keyword=珀莱雅
3\standard=customer_id
3\keyword=商家编号
```

对应 API：`AppConfig::GetMappingKeywords / Set/Add/Remove/Reset/GetEffectiveMappingKeywords()`。

#### 4.4.6 v1.1a 用户体验：6 行顿号表 + 编辑弹窗

RuleSettingDialog Tab6「🧭 表头关键字」布局：

```
┌──────────────────────────────────────────────────────────────────────────────────────────────┐
│ 顶部蓝色提示：每行 1 个标准列，关键字用顿号分隔；蓝色=系统默认，橙色=用户自定义追加         │
│ ⚡ 快速追加关键字到: [▼ 实际重量(KG) ] [毛重、货物重量、计费KG ] [➕添加]                    │
├──────────────┬──────────────┬──────────────────────────────────────────────────┬──────────────┤
│标准列(中文)  │标准列(英文)  │关键字（顿号分隔，双击第 3 列或点右侧✏️编辑）      │操作         │
├──────────────┼──────────────┼──────────────────────────────────────────────────┼──────────────┤
│★订单号       │ order_id     │🟦 order_id、order_no、...        🟧珀莱雅单号     │✏️编辑 🔙恢复此行│
│★目的省份     │ dest_province│🟦 dest_province、province、省份、收件省份…       │✏️编辑 🔙恢复此行│
│★实际重量(KG) │ weight       │🟦 weight、结算重量、毛重…         🟧货物重量       │✏️编辑 🔙恢复此行│
│ 客户编号      │ customer_id │🟦 customer_id、客户…              🟧珀莱雅、商家编号│✏️编辑 🔙恢复此行│
└──────────────┴──────────────┴──────────────────────────────────────────────────┴──────────────┘
底部：🔄 清空所有自定义    ✅ 应用立即生效
```

每行点「✏️ 编辑」/ 双击第 3 列 → 打开多行编辑弹窗：

```
──────────── 编辑关键字：目的省份 ─────────────────────────────────
🟦 系统默认（只读预览）：dest_province、province、目的省份、省份、收件省份 …
🟧 自定义（可换行/顿号/逗号/分号/空格/斜杠/tab，多分隔符自动分词去重）：
  ┌──────────────────────────────────────────┐
  │寄达省份                                  │
  │到件省                                     │
  │目的省名                                   │
  └──────────────────────────────────────────┘
底部按钮：🔤 从顿号字符串粘贴成多行 ｜ 🗑 清空自定义 ｜ 取消 ｜ 💾确认保存
```

#### 4.4.7 「记住映射」回写机制（批量计算对话框）

批量计算确认映射时做了 4 处改动：

| 改动 | 说明 |
|------|------|
| 标准列中文名+★红星必填显示 | 统一从 `AppConfig::StandardColumnToCn()` 取中文名，"目的省份、实际重量(KG)" 红色★前缀 |
| 顶部工具栏 2 个按钮 | 🔄 重置自动映射（重跑一轮 AutoMapColumns）+ 🧭 打开映射规则设置（切到 Tab6）|
| 确认时询问「是否记住」 | Yes → 调 `CalcService::RememberMapping()` 把「当前表头→标准列」对写回 AppConfig 自定义；No → 本次不保存 |
| 入口统一 | `HeaderMappingDialog::IsRememberRequested()` + `batch_calc_dialog.cpp` 调用点 |

代码入口：[HeaderMappingDialog::OnConfirm()](file:///Users/cxd/duckdb/xiaoqiao_freight/src/ui/dialogs/header_mapping_dialog.cpp)。

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
| `GetLicenseFilePath()` | `<data>/license.dat`（授权文件） |
| `GetTimeRecordPath()` | `<data>/time_rec.dat`（防篡改时间戳） |

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

### 4.8 图标管理器

**文件**：[icon_manager.hpp](file:///Users/cxd/duckdb/xiaoqiao_freight/src/ui/icon_manager.hpp) + [icon_manager.cpp](file:///Users/cxd/duckdb/xiaoqiao_freight/src/ui/icon_manager.cpp)

**纯代码绘制**图标（不依赖 PNG 资源），使用 QPainter 程序化绘制，按名称+尺寸+分类缓存到 `QMap<QString, QIcon>`。

#### 尺寸枚举 (IconSize)
SIZE_16 / SIZE_20 / SIZE_24 / SIZE_32 / SIZE_48 / SIZE_64

#### 分类枚举 (IconCategory)
CARD（64px 卡片大图）/ SETTING（24px 设置按钮）/ ACTION（16px 工具栏）/ STATUS（24px 状态）/ LOGO（32px 窗口图标）

#### 便捷方法
```cpp
CardIcon("calc_single")     // 等价 GetIcon(..., CARD, SIZE_64)
SettingIcon("rule_setting") // 等价 GetIcon(..., SETTING, SIZE_24)
ActionIcon("save")          // 等价 GetIcon(..., ACTION, SIZE_16)
StatusIcon("success")       // 等价 GetIcon(..., STATUS, SIZE_24)
```

生成器方法：`GenerateCardIcon` / `GenerateSettingIcon` / `GenerateActionIcon` / `GenerateLogoIcon`（每个方法内部 switch(name) 用 QPainter 绘制对应图形）。

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
    WHEN 'fixed'       THEN s.amount
    WHEN 'percentage'  THEN base_fee * s.amount
    WHEN 'per_weight'  THEN charge_weight * s.amount
    WHEN 'per_volume'  THEN COALESCE(NULLIF(vol_weight, 0), charge_weight) * s.amount  -- v1.1
    ELSE 0
END
```

策略匹配条件（AND）：
1. `is_active = 1`
2. `template_id = 订单.template_id`
3. **作用范围匹配**（满足任意一个即可，多条金额 SUM 叠加）：
   - `strategy_scope = 'global'`
   - 或 `strategy_scope = 'province' AND (surcharge_provinces 中存在该省份)`
   - 或 `strategy_scope = 'customer' AND (surcharge_customers 中 sc.customer_id = rac.customer_id)`（v1.1 新增）
4. **日期生效**（v1.1 新增）：
   ```sql
   AND (
       s.strategy_scope IN ('global','province')
       OR EXISTS(
           SELECT 1 FROM surcharge_date_ranges sd
           WHERE sd.strategy_id = s.strategy_id
             AND CURRENT_DATE BETWEEN sd.start_date AND sd.end_date
       )
   )
   ```
5. 重量范围匹配：
   - `min_weight IS NULL OR charge_weight >= min_weight`
   - `max_weight IS NULL OR max_weight = 0 OR charge_weight <= max_weight`

> **v1.1 修复点**：
> - **H2**：customer 维度原先在 CTE 中根本没 JOIN，导致客户专属策略完全不生效；已补 FULL join。
> - **H3**：date_ranges 原先设计为预留字段未接入；已正式生效。
> - **H5**：偏远地区省份归一化在 `ra.province` 和 `fsc.dest_province` 两侧都做 `REGEXP_REPLACE`，避免 "云南省" vs "云南" 匹配不上。
> - **H4**：表头缺失列时，`CreateNormalizedTable()` 会调用 `col_or_literal_str(num)` 自动返回 `' '/0` 字面量，SQL 不再报"列不存在"致命错误。

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
| **BatchCalcDialog** | batch_calc_dialog.* | QStackedWidget 两页：(1) 文件选择页 — 选输入输出文件、进度条（后台计算期间有 500ms 平滑动画脉冲）、开始按钮（白底+灰边框+悬停变蓝，padding 12px×36px，字号 14px，尺寸大于关闭按钮）；(2) 结果预览页 — QTableWidget 前 N 行 **13 列标准表头**（订单号/客户编号/目的省份/目的城市/实重/体积重/计费重/基础运费/燃油附加费/地区加价/其他附加费/总运费/币种）、结果汇总（总行数/总运费/耗时）、导出、返回。计算结束自动调用 HistoryService.AddHistory()。**v1.1 关键修复 (M1/M4)**：步骤 5（表头映射+建归一化表）、步骤 6（批量计算）、步骤 7（导出 + 历史记录写入）全部用 `QtConcurrent::run` 丢到后台线程执行，`QFutureWatcher` 回调，**完全消除 100 万行级别文件的 UI 卡死**；同时把预览列头从 12 列补到 13 列（增加"地区加价""其他附加费"的独立列展示，不再被合并），解决预览数据错位问题。 |
| **CompareDialog** | compare_dialog.* | 选省份/实重/体积重，点击"对比"后遍历所有模板计算，表格展示各模板的基础运费/燃油费/地区加价/附加费/总运费对比。 |
| **HistoryDialog** | history_dialog.* | 顶部：关键字搜索框 + 起始日期（默认一个月前）+ 结束日期（默认今天）+ 搜索按钮。中部：QTableWidget 任务列表（任务名/输入文件/输出文件/行数/总运费/耗时/状态/创建时间）。底部：打开文件/导出/选中删除/清理旧数据(90天)/关闭。 |
| **RuleSettingDialog** | rule_setting_dialog.* | QTabWidget 5 个 Tab：(1) 运费模板列表（增删改→打开 TemplateEditDialog）；(2) 加价策略表格（增删改，作用域/类型/金额/优先级/启用开关）；(3) 燃油附加费表格（含"启用"切换列 toggle，点击行弹编辑）；(4) 地区加价表格（含"启用"toggle，省/市/区三级）；(5) 预留。 |
| **TemplateEditDialog** | template_edit_dialog.* | 编辑单个模板的完整配置：Tab1 基本信息（名称/快递/首重/续重/体积系数/无重量默认费/默认/描述）；Tab2 阶梯价格表（编辑各分区各阶梯价格）；Tab3 分区-省份关联（调整分区包含哪些省）；Tab4 燃油附加费（同规则设置中的 Tab）。 |
| **CustomerSettingDialog** | customer_setting_dialog.* | 左侧 QListWidget 客户列表（增删改/批量导入）；右侧客户专属报价矩阵表格：10 列表头（报价区域/目的省份/0-0.5KG/0.5-1KG/1-2KG/2-3KG/3-30KG/30KG+/其他/备注）。报价区域列合并单元格，同分区省份连续排列；区域名和省份名只读，价格列双击可编辑；编辑某省份价格时自动同步同分区其他省份；保存时按分区去重写入 tiered_pricing。底部：保存报价按钮。 |
| **SystemSettingDialog** | system_setting_dialog.* | Tab1 性能设置：☑ 自动优化性能（使用系统90%资源）默认勾选；系统信息展示（总内存/CPU核数/自动计算后的内存限制和线程数）；手动内存 MB + 线程数 SpinBox（自动模式下禁用）；Tab2 预留。确定/取消按钮。 |
| **HeaderMappingDialog** | header_mapping_dialog.* | 当 AutoMap 缺失必填列时弹出。左列导入表头 + 右列标准列（必填标红星 *）+ 红色箭头连线可视化映射；点击交互调整；底部 QTableWidget 预览前 5 行数据。确认后返回自定义 mapping。 |
| **AboutDialog** | about_dialog.* | 品牌 Logo、应用名 + "v1.0.0"、公司名、官网（蓝色下划线）、客服电话。授权信息区：机器码（X-XXX-XXX-XXX-XXX，带一键复制按钮）、授权状态（试用版/个人版/企业版/永久版）、有效期（剩余天数）、"授权激活"按钮（弹输入框输入授权码 → LicenseManager.ActivateLicense()）。 |

---

## 8. 授权系统详解

### 8.1 架构概览

```
┌─────────────────────────────────────────────────────┐
│  LicenseManager（主程序内单例）                       │
│    ├─ 机器码生成（MAC 地址 → MD5 → 格式化）          │
│    ├─ 授权码验证（Base64 JSON + HMAC-SHA256 签名）   │
│    ├─ 加密存储（license.dat 保存授权码原文）         │
│    ├─ 启动验证 + 过期/临期弹窗提醒                   │
│    └─ 防时间篡改检测（time_rec.dat 记录上次启动时间） │
└─────────────────────────────────────────────────────┘
                           ↕ 相同算法
┌─────────────────────────────────────────────────────┐
│  授权生成器（独立 GUI 程序 license_generator.app）    │
│    ├─ 输入：机器码 + 授权类型 + 有效期 / 天数        │
│    ├─ 输出：格式化授权码（可复制）                    │
│    └─ 支持校验：粘贴授权码 + 机器码验证签名          │
└─────────────────────────────────────────────────────┘
```

### 8.2 机器码生成

**算法**：
```
1. 遍历所有 QNetworkInterface，取 type=Ethernet 或 Wifi 的硬件地址
2. 拼接所有 MAC 地址字符串
3. 若为空，兜底为 "XIAOQIAO_DEFAULT_MACHINE"
4. QCryptographicHash::Md5(raw) → 32 位 HEX → 大写
5. 按每 4 字符加分隔符：XXXX-XXXX-XXXX-XXXX（共 35 字符）
```

例：`A1B2-C3D4-E5F6-7890`

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

**HMAC 密钥**（v1.1 重要加固，H6 修复）：
```cpp
// 原始密钥：XiaoQiaoFreight2026@#$SecretKey888
// 不再整串写在 .rodata，静态字符串工具 strings 搜不到。改 5 段 runtime 拼接：
constexpr const char kSeg1[] = "XiaoQiao";
constexpr const char kSeg2[] = "Freight2026";
constexpr const char kSeg3[] = "@#$";
constexpr const char kSeg4[] = "SecretKey";
constexpr const char kSeg5[] = "888";
```

> v1.1 同时把 LicenseManager::GetLicenseFilePath() / GetTimeRecordPath() 从原来依赖 `AppConfig` 改成直接 `QStandardPaths::writableLocation(AppDataLocation)`，彻底消除 `license_generator` 链接时因 AppConfig 符号缺失导致的 `Undefined AppConfig` linker error。

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

### 8.6 防时间篡改（v1.1 升级到 3 段持久化格式，M3）

每次启动写入 `time_rec.dat`，**v1.1 升级格式**：
```
<上次启动时间 yyyyMMddHHmmss>|<累计运行秒>|<是否已检测到篡改 0/1>
```

下次启动检查：
- 如果 `当前时间 < 上次启动时间` → 判定时间回拨
- 按回拨天数扣减授权有效期（天数=⌈秒数/86400⌉）
- 检测到篡改后：
  1. **永久版豁免**（不扣 expire_date，也不打标记）
  2. **激活过的试用/个人/企业版**：把缩短后的 expire_date **立即写回 license.dat**（持久化），避免用户重启机器就复原
  3. 第 3 段 `tampered_flag=1` 置位，下次启动直接判改，无需再比较时间戳
- `time_tampered_ = true` 会使启动弹窗提醒"检测到时间被篡改"

> 原来 v1.0 只有 `<时间>|<秒数>` 两段，篡改标记仅在内存中，**重启就失效**，用户很容易绕过。v1.1 升级到三段持久化彻底修复了这个安全漏洞。

### 8.7 启动提醒分级（CheckStartupReminder）

| 状态 | show_expired | show_near_expiry | 提示内容 |
|------|:-----------:|:---------------:|---------|
| time_tampered | ✅ | - | 检测到系统时间被篡改 + 购买正版提示 |
| 授权已过期（已激活） | ✅ | - | 授权已过期 + 续费电话 |
| 试用期结束（未激活） | ✅ | - | 试用期结束 + 购买电话 |
| 剩余天数 ≤ 7（已激活） | - | ✅ | N 天后到期 + 续费电话 |
| 剩余天数 ≤ 7（未激活） | - | ✅ | 试用期还剩 N 天 + 购买电话 |

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

### 9.6 v1.1 新增踩坑记录

#### ❌ 坑 14：QSqlDatabase removeDatabase 报 "still in use"
**现象**：控制台输出 `QSqlDatabasePrivate::removeDatabase: connection 'duckdb_import_1' is still in use, all queries will cease to work.` 不影响功能但告警刺眼。  
**原因**：QSqlDatabase/QSqlQuery 的析构顺序和 Qt 内部共享指针周期不一致，函数结束时 QSqlDriver 仍持有引用。  
**解决**：
1. 连接名加自增后缀 `duckdb_import_N` / `rules_conn_N`，避免重名。
2. 把 `QSqlQuery q` 放入花括号内显式作用域，先销毁 query。
3. `close()` 后用 `QTimer::singleShot(0, ...)` 调度到下一事件循环再 removeDatabase。

#### ❌ 坑 15：百万级文件批量计算 UI 卡死
**现象**：100 万行计算 + 导出 10~20 秒之间进度条不动，鼠标转圈，窗口报"应用未响应"。  
**原因**：BatchCalcDialog 的"开始计算"按钮触发的所有逻辑（表头映射、计算、导出、写历史）都在主线程执行。  
**解决**：`QtConcurrent::run` + `QFutureWatcher<CalcContext>` 把第 5-7 步全部丢到后台线程；主线程启动一个 `QTimer(500ms)` 做进度条脉冲动画，在 watcher 的 finished 信号回调里结束动画并跳到结果预览页。

#### ❌ 坑 16：QVariant 类型不全导致数值列错位（H7）
**现象**：SQLite 里存的 BOOL/UInt/Float 读出来拼 INSERT 时，默认 `QVariant::toString()` 返回 `"true"/"1234"/"3.14000002"` 的字符串形式，被 DuckDB 当作 VARCHAR 入数值列，要么 NULL 要么显式截断。  
**解决**：`LoadRulesFromSQLite()` 里 `switch (v.metaType().id())` 显式分 Case 处理 `Bool/Int/UInt/LongLong/ULongLong/Float/Double` 全量数值形态。

---

## 10. 部署与打包

### 10.1 编译（macOS）

前置依赖：Qt6 6.5+、CMake 3.20+、duckdb 1.5.4 库和头文件。

```bash
cd xiaoqiao_freight
mkdir build && cd build
cmake .. -DCMAKE_PREFIX_PATH=/opt/homebrew/opt/qt6
make -j$(sysctl -n hw.ncpu)             # 一次构建 3 个目标
# 等价于单目标：
# make -j xiaoqiao_freight license_generator batch_runner
```

编译产物位置：
```
build/bin/
├── xiaoqiao_freight.app       # 主程序（含自动部署）
├── license_generator.app      # 授权生成器（含自动部署）
└── batch_runner               # 命令行批量计算工具（console 可执行，无 GUI）
```

#### batch_runner 独立用法

```bash
# 基本：输出目录默认取输入同目录下 /小乔计算结果
./build/bin/batch_runner  "/path/to/帐单1.xlsx"  "/path/to/帐单2.xlsx" ...

# 指定输出目录
./build/bin/batch_runner  -o /tmp/xq_results     "/path/to/*.xlsx"
```
处理过程中会输出每个文件的表头自动映射结果、输出行数、合计运费、耗时；结束时汇总成功/失败数量及输出目录。详见 §14。

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
| HMAC 密钥 5 段拼接 | [license_manager.cpp](file:///Users/cxd/duckdb/xiaoqiao_freight/src/core/license_manager.cpp#L18-L51) | `kSeg1~kSeg5, BuildKey()` |
| 机器码生成 | [license_manager.cpp](file:///Users/cxd/duckdb/xiaoqiao_freight/src/core/license_manager.cpp#L125-L150) | `GenerateMachineCode()` |
| 授权码生成 | [license_manager.cpp](file:///Users/cxd/duckdb/xiaoqiao_freight/src/core/license_manager.cpp#L152-L180) | `GenerateLicenseKey()` |
| 授权码验证 | [license_manager.cpp](file:///Users/cxd/duckdb/xiaoqiao_freight/src/core/license_manager.cpp#L182-L228) | `VerifyLicenseKey()` |
| 防时间篡改检测 (3 段持久化) | [license_manager.cpp](file:///Users/cxd/duckdb/xiaoqiao_freight/src/core/license_manager.cpp#L320-L423) | `CheckTimeTampering()` |
| 全功能入口授权统一检查 | [main_window.cpp](file:///Users/cxd/duckdb/xiaoqiao_freight/src/ui/main_window.cpp) | `CheckLicenseOrWarn()` |
| DuckDB 初始化 + 性能配置 + excel 扩展加载顺序 | [duckdb_manager.cpp](file:///Users/cxd/duckdb/xiaoqiao_freight/src/db/duckdb_manager.cpp#L17-L99) | `DuckDBManager::Init()` |
| 从 SQLite 同步 12 张规则表到 DuckDB（QVariant 全类型 + 延迟 removeDatabase） | [duckdb_manager.cpp](file:///Users/cxd/duckdb/xiaoqiao_freight/src/db/duckdb_manager.cpp#L101-L304) | `LoadRulesFromSQLite()` |
| TableExists 用 SHOW TABLES LIKE | [duckdb_manager.cpp](file:///Users/cxd/duckdb/xiaoqiao_freight/src/db/duckdb_manager.cpp) | `TableExists()` |
| 规则库 schema 版本升级 (need_init_default 防重复) | [sqlite_rule_repository.cpp](file:///Users/cxd/duckdb/xiaoqiao_freight/src/db/sqlite_rule_repository.cpp#L38-L118) | `SqliteRuleRepository::Init()` |
| 默认中通模板(6分区×6阶梯) | [sqlite_rule_repository.cpp](file:///Users/cxd/duckdb/xiaoqiao_freight/src/db/sqlite_rule_repository.cpp#L228-L328) | `CreateDefaultTemplate()` |
| 6 个重量阶梯定义列表 | [sqlite_rule_repository.cpp](file:///Users/cxd/duckdb/xiaoqiao_freight/src/db/sqlite_rule_repository.cpp#L299-L306) | `tiers` 局部变量 |
| 6 个分区定义 | [sqlite_rule_repository.cpp](file:///Users/cxd/duckdb/xiaoqiao_freight/src/db/sqlite_rule_repository.cpp#L263-L276) | `zones` 局部变量 |
| 新增客户时自动创建专属报价 | [sqlite_rule_repository.cpp](file:///Users/cxd/duckdb/xiaoqiao_freight/src/db/sqlite_rule_repository.cpp#L718-L820) | `AddCustomer()` |
| 单条运费计算 SQL（同步 customer_id/vol_weight/per_volume） | [calc_service.cpp](file:///Users/cxd/duckdb/xiaoqiao_freight/src/services/calc_service.cpp#L33-L157) | `CalcSingle()` |
| 10 层 CTE 批量计算 SQL（含 per_volume + surcharge_customers + surcharge_date_ranges） | [calc_service.cpp](file:///Users/cxd/duckdb/xiaoqiao_freight/src/services/calc_service.cpp#L264-L442) | `BuildCalcSQL()` |
| 缺列自动替换为字面量（SQL 合法修复） | [calc_service.cpp](file:///Users/cxd/duckdb/xiaoqiao_freight/src/services/calc_service.cpp#L503-L514) | `col_or_literal_str/num()` |
| 两轮表头自动映射 | [calc_service.cpp](file:///Users/cxd/duckdb/xiaoqiao_freight/src/services/calc_service.cpp#L564-L612) | `AutoMapColumns()` |
| TRY_CAST + COALESCE 创建标准化表 | [calc_service.cpp](file:///Users/cxd/duckdb/xiaoqiao_freight/src/services/calc_service.cpp#L614-L648) | `CreateNormalizedTable()` |
| 计算进度信号（异步 UI 用） | [calc_service.hpp](file:///Users/cxd/duckdb/xiaoqiao_freight/src/services/calc_service.hpp) | `ProgressChanged/CalcFinished` |
| 批量计算对话框（QtConcurrent 异步 + 13 列预览头） | [batch_calc_dialog.cpp](file:///Users/cxd/duckdb/xiaoqiao_freight/src/ui/dialogs/batch_calc_dialog.cpp#L389-L598) | `OnStartCalc() + watcher_` |
| 历史记录 CRUD | [history_service.cpp](file:///Users/cxd/duckdb/xiaoqiao_freight/src/services/history_service.cpp) | `AddHistory/QueryHistory/DeleteHistory/CleanupOldData` |
| 主窗口布局 + QSS 样式 | [main_window.cpp](file:///Users/cxd/duckdb/xiaoqiao_freight/src/ui/main_window.cpp#L31-L229) | `SetupUI()/SetupStyles()` |
| 页脚官网链接（外链+下划线） | [main_window.cpp](file:///Users/cxd/duckdb/xiaoqiao_freight/src/ui/main_window.cpp#L133-L142) | footer_label_ |
| 授权启动后检查弹窗 | [main_window.cpp](file:///Users/cxd/duckdb/xiaoqiao_freight/src/ui/main_window.cpp#L300-L313) | `CheckLicenseStartup()` |
| macdeployqt + 签名修复脚本 | [deploy_mac.sh](file:///Users/cxd/duckdb/xiaoqiao_freight/scripts/deploy_mac.sh) | 第 1-46 行 |
| 批处理命令行工具入口 | [batch_runner/main.cpp](file:///Users/cxd/duckdb/xiaoqiao_freight/tools/batch_runner/main.cpp) | `main()` / `ProcessOne()` |
| CMake 3 目标构建 | [CMakeLists.txt](file:///Users/cxd/duckdb/xiaoqiao_freight/CMakeLists.txt) | `xiaoqiao_freight / license_generator / batch_runner` |

---

## 13. 版本变更日志 v1.1（Bug 修复总览）

**生成日期**：2026-07-24  
**修复项数量**：严重 7 + 中级 5 + 低级 8 = 共 **20 项**

### 🔴 严重级（影响正确性 / 安全 / 反盗版，必须修复）

| 编号 | 标题 | 根因 | 修复方式 |
|------|------|------|---------|
| H1 | 默认规则/阶梯/策略重复插入 → 运费翻倍 | 大版本升级 + IsFirstRun 都各自触发 InitDefaultData() 没有互斥判断 | `need_init_default = (current_version < 10) \|\| IsFirstRun()` 合并条件 |
| H2 | 客户级 surcharge_strategies 永远不生效 | `BuildCalcSQL` 里未 JOIN `surcharge_customers`，strategy_scope='customer' 直接被忽略 | 子查询 JOIN surcharge_customers + `s.scope='customer' AND sc.customer_id=rac.customer_id` |
| H3 | 日期区间 surcharge_date_ranges 设计为预留但未接入计算 | 同上，日期表从未关联 | 子查询 JOIN surcharge_date_ranges + `CURRENT_DATE BETWEEN start_date AND end_date` |
| H4 | Excel 缺列时，SQL 里列名空串 → SQL 错误 "syntax error" | `BuildCalcSQL` 缺列后直接把空字符串当列名拼 `""dest_province""` | 新增 helper `col_or_literal_str/num`：空列名 → 返回 `' '` 或 `0` 字面量 |
| H5 | 偏远地区省份"云南省" vs 归一化"云南"匹配不上 | 归一化只作用于 fsc.dest_province 一侧，ra.province 从 SQLite 同步来还是全名 | `REGEXP_REPLACE` 两侧都加，双向归一化 |
| H6 | HMAC 密钥整串在 .rodata 里，`strings app | grep XiaoQiao` 直接搜到 → 授权被伪造 | 硬编码 `const char*` 全量密钥 | 拆成 5 段 `constexpr char[]`（XiaoQiao / Freight2026 / @#$ / SecretKey / 888）运行时拼接 |
| H7 | QVariant 类型不全（UInt/Bool/Float）被当字符串插入数值列 → 金额错位 | `LoadRulesFromSQLite` 默认 `QVariant::toString()` | `switch (v.metaType().id())` 覆盖 7 种数值形态全部 toString 走数值分支 + `'包裹字符串'` |

### 🟠 中级（体验 / 稳定性）

| 编号 | 标题 | 根因 | 修复方式 |
|------|------|------|---------|
| M1 | 100 万行批量计算 UI 卡死/未响应 | 表头映射、计算、导出、写历史都阻塞主线程 | `QtConcurrent::run` + `QFutureWatcher<CalcContext>` 后台做步骤 5-7；QTimer 500ms 进度条动画 |
| M2 | Reload 规则时 Qt 告警 "duckdb_import 连接名重复" | 连接名硬编码常量 | `static int counter_` 生成 `duckdb_import_N` 和 `rules_conn_N` 自增 |
| M3 | 时间篡改惩罚重启清零；永久版也被扣分 | tampered_flag 只存内存，没有豁免永久版 | `time_rec.dat` 升级为 3 段格式（时间\|秒\|篡改标）；永久版检测到篡改不扣分不写标 |
| M4 | 批量预览表列头错位（"地区加价""其他附加费" 缺失，总运费右移 2 格） | headers 列表写了 12 个列但 SQL SELECT 出 13 列 | 13 列表头严格对齐 SQL 输出顺序 |
| M5 | `TableExists()` 偶发 false → 后续 CreateTable 报 "already exists" | DuckDB 1.5.x information_schema 查询不稳定（可能和 schema cache 有关）| 改用 `SHOW TABLES LIKE` + `DESCRIBE SELECT LIMIT 1` 双路径兜底 |

### 🟢 低级（代码质量 / 规范 / 链接问题）

| 编号 | 标题 | 修复方式 |
|------|------|---------|
| L1 | 只有批量计算入口有授权校验，单算/比价/历史/规则设置等入口无限制 | 在主窗口 6 个入口统一调用 `CheckLicenseOrWarn()` |
| L2 | AppConfig 所有实现都写在头文件内，改一行全项目重编 | 所有方法实现改写到 `app_config.cpp` |
| L3 | 每次启动 `INSTALL excel; LOAD excel` 重复检查扩展 | 先 LOAD，失败才 INSTALL + LOAD |
| L4 | RuleService::InitDefaultData 声明为空壳但头文件里 | 移到 rule_service.cpp 并注释"由 Repository 内部触发，外部不调用" |
| L5 | 单算 SQL 逻辑滞后于批量 SQL（缺 per_volume/customer/date_ranges）| 单算 SQL 同步补齐全部 4 个参数 |
| L6 | 阶梯文案误写 "0.51KG-1KG"（应该是 0.5KG 到 1KG）| 修正 tier_0.5_1 的 UI 文案 |
| L7 | ExportToFile 不认 .xls 后缀直接报错 | `.xls` 走 xlsx 格式（Excel 可直接打开兼容）|
| L8 | CalcService 声明了 ProgressChanged/CalcFinished signal 却从不 emit | 在 CalcBatch / CalcFromFile 关键节点 emit 真实进度 |

### 🔗 链接构建修复
- **license_generator 链接错误 Undefined AppConfig**：`GetLicenseFilePath() / GetTimeRecordPath()` 不再依赖 `AppConfig` 单例，直接用 `QStandardPaths::writableLocation(AppDataLocation)`。

### 13.1 v1.1a 功能增强：表头关键字 UX 重设计（用户可自定义顿号关键字）

**新增功能数**：3 个核心能力（标准列中文显示、映射关键字自定义、记住映射写回），涉及 4 个源文件 + 2 个 UI 入口。

| 编号 | 功能 | 实现位置 | 说明 |
|:----:|------|---------|------|
| U1 | 标准列统一中文表（含★必填红星）| `AppConfig::StandardColumnChinese() / StandardColumnOrder() / RequiredStandardColumns()` | `header_mapping_dialog.cpp` 右侧列表、`rule_setting_dialog.cpp` Tab6 第一列、映射确认对话框内所有文案，统一调这三个静态函数 |
| U2 | Tab6「🧭 表头关键字」6 行顿号表 + 编辑弹窗 | `RuleSettingDialog::SetupMappingTab() + OnEditMappingRow()` | 原 70+ 行关键字表浓缩成 6 行；顿号分隔渲染（蓝🟦默认/橙🟧自定义富文本）；双击关键字列或点 ✏️ 编辑打开多行编辑弹窗 + 🔤 顿号粘贴助手 |
| U3 | 顶部「⚡ 快速追加关键字到 [▼] [输入] [➕添加]」 | `OnQuickAddKeyword()` + `split_keywords()` 多分隔符分词 | 一行工具栏批量加关键字（顿号/逗号/分号/斜杠/换行 tab 全部识别，自动拆分去重） |
| U4 | 每行单独「🔙 恢复此行默认」 / 全局「🔄 清空所有自定义」| `OnResetMappingRow() + OnResetMappingKeywords()` | 支持单行/全量回滚 |
| U5 | 映射对话框：标准列中文名★红星 + 🔄重置映射 + 🧭跳转到 Tab6 | `HeaderMappingDialog::SetupUI()` 顶部按钮条 | 用户在批量计算途中发现不对就能直接跳到 Tab6 改关键字，关窗后自动再跑一轮「🔄重置自动映射」 |
| U6 | 「记住这些对应关系，下次自动识别？」询问 | `HeaderMappingDialog::OnConfirm()` + `IsRememberRequested()` 标志位 + `CalcService::RememberMapping()` | 用户选"Yes"时，把当前 `actual_col -> standard` 对作为「自定义关键字」追加到 AppConfig，下次同一份表头就不需要手动拖了 |

> 源码改动文件：`app_config.{hpp,cpp}` / `calc_service.{hpp,cpp}` / `rule_setting_dialog.{hpp,cpp}` / `header_mapping_dialog.{hpp,cpp}` / `batch_calc_dialog.cpp`（调用 `RememberMapping`）。

---

## 14. 批处理命令行工具 batch_runner

**源码**：[tools/batch_runner/main.cpp](file:///Users/cxd/duckdb/xiaoqiao_freight/tools/batch_runner/main.cpp)  
**构建命令**：`make batch_runner`（或直接全量 make）  
**二进制位置**：`build/bin/batch_runner`（非 app bundle，纯 macOS console 可执行）

### 设计目标
1. 在无 GUI 环境/脚本/CI 下跑批量计算
2. 完全复用 `CalcService` + `DuckDBManager`，计算结果 1:1 等同于 GUI 内点"开始计算"
3. 输出结构化统计（行数/合计运费/耗时）方便后续管道处理

### 命令行参数

```
batch_runner  [-o OUTPUT_DIR]  INPUT1 [INPUT2 ...]
```

| 参数 | 说明 | 默认 |
|------|------|------|
| `-o <dir>` | 结果文件统一输出目录（会自动 mkdir -p） | 第一个输入文件的同目录 `/小乔计算结果/` |
| `INPUT` | xlsx/csv/parquet 文件，可多个 / 支持 shell glob | 必填 |

### 典型返回值

```
Exit 0  → 全部文件成功
Exit 10 → ≥1 文件失败（统计里有失败数）
Exit 2~5 → 初始化阶段失败 (AppConfig / rules.db / DuckDB / License)
```

### 内部流程
```
main()
  ├─ QCoreApplication (不需要 Widgets/Gui)
  ├─ AppConfig.Init
  ├─ LicenseManager.Init  (授权状态会打印到 stdout, 超期则失败)
  ├─ SqliteRuleRepository.Init
  ├─ DuckDBManager.Init + LoadRulesFromSQLite(rules.db)
  └─ for each INPUT:
        ├─ qInfo() << "处理: <文件名>"
        ├─ CalcFromFile(INPUT, <output_dir>/<basename>_运费结果.<ext>)
        ├─ DuckDB SELECT COUNT, SUM(总运费) FROM _output_tmp
        └─ qInfo() << "[OK/FAIL] 行数=N  合计运费=X  耗时=T"
```

> 无需 Widgets 模块，但仍然链接了 `Qt6::Core Sql Concurrent Network` 和 DuckDB 库，因此可以直接在终端/shell 脚本里使用。

---

## 15. 性能实测基线（真实 260 万行帐单）

测试日期：2026-07-24  
测试机器：Apple Silicon arm64, 8GB RAM, 8 逻辑核  
程序版本：v1.1, DuckDB 1.5.4, Qt 6.5+  
DuckDB 参数：memory_limit 7372MB, threads=7, excel 扩展已预热  
数据来源：`/Users/cxd/帐单/` 真实客户月帐单 × 4 份

### 总览

| 文件 | 行数（输出） | 运费合计（元）| 端到端耗时（读+算+写） | 吞吐 |
|------|------------:|--------------:|----------------------:|-----:|
| 珀莱雅-4月发件账单.xlsx | 751,800 | 2,733,460.51 | 12.9 s | **58,300 行/秒** |
| 蜜丝婷-4月发件账单表1.xlsx | 962,522 | 3,464,693.72 | 15.9 s | **60,500 行/秒** |
| 蜜丝婷-4月发件账单表2.xlsx | 419,265 | 1,436,388.97 | 6.9 s | **60,800 行/秒** |
| 蜜丝婷-5月发件账单.xlsx | 496,319 | 1,757,814.75 | 8.4 s | **59,100 行/秒** |
| **合计** | **2,629,906** | **¥9,392,357.95** | **44.1 s** | **59,700 行/秒** |

### 阶段耗时分解（以 96 万行为例）

| 阶段 | 耗时 | 占比 | 瓶颈 |
|------|-----:|-----:|------|
| Excel 读入（read_xlsx + 解压 + 字符串解析） | 6.4 s | 40 % | IO/解压 |
| 10 层 CTE SQL 运费计算（含分区/阶梯/燃油/偏远/策略） | 7.1 s | 45 % | CPU |
| 结果写 xlsx（COPY FORMAT xlsx + zip 压缩） | 2.4 s | 15 % | IO/压缩 |

### 表头自动映射验证（100% 全命中）

```
运单号 → order_id | 目的省份 → dest_province | 目的城市 → dest_city
结算重量 → weight | 体积重 → vol_weight      | 客户 → customer_id
```
6/6 必填/关键列**精确匹配**，无需人工介入。

### 对比参考（100 万行同量级方案对比）

| 方案 | 典型耗时 |
|------|---------:|
| 小乔运费结算 v1.1（DuckDB 7 线程）| **16.8 s** |
| Polars（Rust）全核 | 8–12 s |
| Python + pandas 单线程 | 180–360 s |
| Excel 手工 VLOOKUP | 30–120 min |
| SQLite | 20–40 min |

---

## 16. 性能优化路线图（完整方案 + 实施步骤）

> 独立文档：[PERFORMANCE_OPTIMIZATION.md](file:///Users/cxd/duckdb/xiaoqiao_freight/PERFORMANCE_OPTIMIZATION.md)  
> 测试基准：Apple Silicon arm64 / 8GB / 8 核 / DuckDB 1.5.4，4 份真实帐单 **2,629,906 行**，端到端总耗时 **44.1 s**，吞吐 ≈ 59,600 行/秒。
> 阶段耗时分解（以 96 万行为例）：读 xlsx 40% · 10 层 CTE 计算 45% · 写 xlsx 15%。

### 16.0 快速决策表

| 方案 | 预估收益 | 工作量 | 侵入性 | 推荐顺序 |
|------|---------:|-------:|-------:|:--------:|
| 方案一：Parquet 热缓存 + 并行 xlsx Reader | +30~40% | 0.5 天 | 低 | **①** |
| 方案二：子查询 → 预聚合 CTE + LATERAL JOIN | +60~80% | 1.5~2 天 | 中 | **②** |
| 方案三：列式 Parquet 主路径 + xlsx 异步转码 | +200~300% | 2~3 天 | 高 | ③ |

---

### 16.1 方案一：Parquet 热缓存 + 并行 xlsx Reader（预估 +30~40%，落地最快）

**收益**：100 万行 16s → 10~11s；首读慢、**同文件二次运行读时间立减 60%**。

#### 改造思路
1. `DuckDBManager::ImportFromFile()` 遇到 `.xlsx/.csv` 时：
   - 首次导入后 `COPY _input_tmp TO '缓存.parquet' (FORMAT PARQUET)`；
   - 用「文件绝对路径 + mtime + size」做 key，存 `QCache<QString,QString>`（LRU 50 项）；
   - 下次命中缓存时 `read_parquet` 代替 `read_xlsx`（列存零解压，100 万行读入 ≈ 0.4 s）。
2. 追加 DuckDB 并行/缓存参数（`DuckDBManager::Init()` 末尾）：
```sql
SET threads = <CPU 核数>;
SET preserve_insertion_order = false;  -- 允许算子重排
SET enable_object_cache = true;       -- 复用 xlsx 解压页缓存
```

#### 涉及文件
- [duckdb_manager.cpp](file:///Users/cxd/duckdb/xiaoqiao_freight/src/db/duckdb_manager.cpp) — `Init()` 加 SET；`ImportFromFile()` 加缓存分支
- [app_config.cpp](file:///Users/cxd/duckdb/xiaoqiao_freight/src/core/app_config.cpp) — 复用 `GetCacheDir()`（已有方法）

#### 风险/副作用
- Parquet ≈ xlsx 的 1/5 体积，但 100 份历史帐单会占 2~5 GB；建议加 `max_cache_mb`（默认 2 GB）+ 启动 LRU 清理
- 避免读脏数据 → key 加文件头部 4 KB SHA1（比只看 mtime/size 安全）

---

### 16.2 方案二：子查询改预聚合 CTE + LATERAL JOIN（预估 +60~80%，只改 SQL）

**收益**：不依赖缓存、每次都加速；100 万行 16s → 3~4s；CPU 占用从 700% 拉到 780%。

#### 改造思路
当前 3 处相关子查询（`remote_areas / surcharge_strategies / fuel_surcharge`）对大表走 nested-loop：行数 × 维度表行数 ≈ 96 万 × 288 ≈ 2.7 亿次 inner loop。

改成**CTE 预聚合 + 单 LEFT JOIN**：
```sql
-- 改造前（相关子查询，每行执行一次）：
COALESCE((SELECT SUM(ra.surcharge) FROM remote_areas ra WHERE ... = rac.dest_province),0)

-- 改造后（预聚合一次，整表 JOIN）：
remote_areas_agg AS (
  SELECT template_id, province, city, district, SUM(surcharge) AS remote_surcharge
  FROM remote_areas WHERE is_active = 1
  GROUP BY template_id, province, city, district
)
SELECT bfc.*, COALESCE(ra.remote_surcharge, 0) AS remote_surcharge
FROM base_fee_calc bfc
LEFT JOIN remote_areas_agg ra
  ON ra.template_id = bfc.template_id
 AND REGEXP_REPLACE(ra.province,...) = REGEXP_REPLACE(bfc.dest_province,...)
 AND (ra.city IS NULL OR ra.city = bfc.dest_city)
 AND (ra.district IS NULL OR ra.district = '')
```
`surcharge_strategies` 同理：customer / province / global 三类 scope 先 **UNION ALL** 成宽表再 LEFT JOIN 一次。

#### 涉及文件
- [calc_service.cpp BuildCalcSQL()](file:///Users/cxd/duckdb/xiaoqiao_freight/src/services/calc_service.cpp#L264-L442) — 把子查询替换成预聚合 CTE
- [calc_service.cpp CalcSingle()](file:///Users/cxd/duckdb/xiaoqiao_freight/src/services/calc_service.cpp#L94-L164) — 同步 SQL 形状（保持单算/批量完全一致，避免结果漂移）

#### 验证方法
用 batch_runner 单独跑 96 万行那份，目标：15.9s → <6s（SQL 7.1s → 1.5~2s）。

#### 风险
- SQL 从 200 行变成 350 行 → Debug 模式保留旧版 `BuildCalcSQL_LoopJoin()` 做结果 diff 校验（bit-exact SUM 必须一致）
- GROUP BY 维度不重复 → 单测覆盖 3 条样例（全国/省内/特定客户）

---

### 16.3 方案三：列式 Parquet 主路径 + xlsx 异步转码（预估 +200~300%）

**收益**：对「内部算帐/回库」场景完全替代 xlsx，100 万行端到端 **<3 秒**；对外发 Excel 时后台转码。

#### 改造思路
- 计算主路径默认输出 parquet（xlsx 的 5~10× 写出速度，1/10 文件体积）
- UI 侧「生成对外 xlsx」改成 QtConcurrent 后台异步任务，用户不阻塞：
```cpp
COPY result TO '结果.parquet' (FORMAT PARQUET);  // 0.3s 主线程阻塞
/* QtConcurrent 后台 */ COPY result TO '结果.xlsx' (FORMAT xlsx);  // 2.4s 异步
```
- 列头中文缓存为 Parquet `field_id` 元数据，打开时用 VIEW 映射。

#### 涉及文件
- [batch_calc_dialog.cpp](file:///Users/cxd/duckdb/xiaoqiao_freight/src/ui/dialogs/batch_calc_dialog.cpp#L498-L502) — 默认改 `.parquet`，保留"另存为 xlsx"异步任务
- [duckdb_manager.cpp ExportToFile()](file:///Users/cxd/duckdb/xiaoqiao_freight/src/db/duckdb_manager.cpp#L322-L349) — parquet 阻塞、xlsx 新增重载返回 `std::future<bool>`

#### 风险
- Parquet 不能被财务直接打开 → **默认对外仍然异步发 xlsx**，Parquet 仅后台主路径
- HistoryService 存 parquet 路径，避免回查时旧结果 xlsx 找不到

---

### 16.4 统一验证脚本（每次改完跑一次）

```bash
cd /Users/cxd/duckdb/xiaoqiao_freight
cmake -S . -B build -DCMAKE_PREFIX_PATH=/opt/homebrew/opt/qt6 >/dev/null
make -C build -j$(sysctl -n hw.ncpu) batch_runner xiaoqiao_freight 2>&1 | tail -5

mkdir -p /tmp/xq_bench && rm -rf /tmp/xq_bench/*
INPUT_DIR="/Users/cxd/帐单"
for f in "$INPUT_DIR"/*.xlsx; do
  echo ">>> $f"
  command time -f 'real=%e user=%U sys=%S maxmem=%MKB' \
    ./build/bin/batch_runner -o /tmp/xq_bench "$f" 2>&1 | grep -E '\[OK\]|行数=|合计运费|\[FAIL\]'
done
ls -lah /tmp/xq_bench
```

#### 关键比较指标（回滚阈值）
| 指标 | 判定标准 |
|------|---------|
| Wall time `real=Xs` | 用户体感耗时，必须 < 基线 59,600 行/秒 |
| Max RSS `maxmem=KB` | 16GB 以下机器 < 7GB，防止 OOM |
| 输出行数一致性 | ±0.1% 以内（COALESCE 空行过滤差异算正常）|
| **SUM(总运费) 一致性** | 与基线 `¥9,392,357.95` **逐位 bit-exact 一致**，否则 SQL 改写回滚 |

---

### 16.5 快速兜底：DuckDB SET 不改代码就能 +15%

临时加速（建议把这 5 条做成「系统设置 → 性能档位：标准/极速」切换项，未来加 UI）：

```sql
SET threads = 8;
SET memory_limit = '7GB';
SET preserve_insertion_order = false;
SET enable_object_cache = true;
SET temp_directory = '/tmp/duckdb-tmp';   -- 防 8GB 机器跑 500 万行 OOM
```

目前只需要在 [duckdb_manager.cpp Init()](file:///Users/cxd/duckdb/xiaoqiao_freight/src/db/duckdb_manager.cpp) 末尾追加 `con.Query("...")`，`ApplyAutoPerformance()` 已经自动算好 threads/memory，拿数值直接拼 SQL 即可。

---

*文档版本：1.1（正式版）*  
*最后更新：2026-07-24（v1.1a：新增 §13.1 表头关键字 UX 升级记录 + 补全 §16 性能优化三方案详细步骤 + 验证脚本）*  
*版权所有 © 2026 杭州喵喵至家网络有限公司 www.hbdxm.com*
