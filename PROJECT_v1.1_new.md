# 小乔运费结算系统 - 项目文档

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
├── CMakeLists.txt              # CMake 构建配置（主程序 + 授权生成器）
├── PROJECT.md                  # 本项目文档
├── .gitignore                  # Git 忽略规则
├── resources/
│   └── resources.qrc           # Qt 资源文件（预留图标等资源）
├── scripts/
│   └── deploy_mac.sh           # macOS 部署脚本（macdeployqt + 签名修复 + 去隔离属性）
├── tools/
│   └── license_generator/      # 授权生成器工具（独立 GUI 程序）
│       ├── main.cpp
│       ├── license_generator_widget.hpp
│       └── license_generator_widget.cpp
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
    │   ├── calc_service.hpp           # 计算服务（单条/批量/文件）
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
            ├── batch_calc_dialog.*      # 批量计算
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
| per_weight | `charge_weight × amount`（每公斤金额） |

**其他属性**：
- `priority`：优先级（目前仅排序，金额是 SUM 叠加）
- `min_weight / max_weight`：重量门槛（0 或 NULL 表示不限）
- `is_active`：启用开关
- `surcharge_date_ranges`：预留的日期范围 + 周几生效机制（暂未接入计算 SQL）

**内置默认策略 3 条**：
1. `packing_fee`：包装服务费，全局固定 1 元
2. `remote_xz_xj`：新疆西藏地区加价，按重量 2 元/KG
3. `peak_season`：旺季附加费，全局 10%（可随时开关）

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
    WHEN 'fixed'      THEN s.amount
    WHEN 'percentage' THEN base_fee * s.amount
    WHEN 'per_weight' THEN charge_weight * s.amount
    ELSE 0
END
```

策略匹配条件（AND）：
1. `is_active = 1`
2. `template_id = 订单.template_id`
3. 作用范围匹配：
   - `strategy_scope = 'global'`
   - 或 `strategy_scope = 'province' AND (surcharge_provinces 中存在该省份)`
4. 重量范围匹配：
   - `min_weight IS NULL OR charge_weight >= min_weight`
   - `max_weight IS NULL OR max_weight = 0 OR charge_weight <= max_weight`

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

### 8.6 防时间篡改

每次启动写入 `time_rec.dat`，内容格式：
```
<上次启动时间 yyyyMMddHHmmss>|<累计运行秒>
```

下次启动检查：
- 如果 `当前时间 < 上次启动时间` → 判定时间回拨
- 按回拨天数扣减授权有效期（天数=⌈秒数/86400⌉）
- 永久版不扣减
- `time_tampered_ = true` 会使下一次启动提醒直接判过期

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
| 机器码生成 | [license_manager.cpp](file:///Users/cxd/duckdb/xiaoqiao_freight/src/core/license_manager.cpp#L37-L63) | `GenerateMachineCode()` |
| 授权码生成 | [license_manager.cpp](file:///Users/cxd/duckdb/xiaoqiao_freight/src/core/license_manager.cpp#L65-L95) | `GenerateLicenseKey()` |
| 授权码验证 | [license_manager.cpp](file:///Users/cxd/duckdb/xiaoqiao_freight/src/core/license_manager.cpp#L97-L148) | `VerifyLicenseKey()` |
| 防时间篡改检测 | [license_manager.cpp](file:///Users/cxd/duckdb/xiaoqiao_freight/src/core/license_manager.cpp#L303-L358) | `CheckTimeTampering()` |
| DuckDB 初始化+性能配置 | [duckdb_manager.cpp](file:///Users/cxd/duckdb/xiaoqiao_freight/src/db/duckdb_manager.cpp#L17-L50) | `DuckDBManager::Init()` |
| 从 SQLite 同步 12 张规则表到 DuckDB | [duckdb_manager.cpp](file:///Users/cxd/duckdb/xiaoqiao_freight/src/db/duckdb_manager.cpp#L52-L247) | `LoadRulesFromSQLite()` |
| 规则库 schema 版本升级(v10→v11) | [sqlite_rule_repository.cpp](file:///Users/cxd/duckdb/xiaoqiao_freight/src/db/sqlite_rule_repository.cpp#L38-L94) | `SqliteRuleRepository::Init()` |
| 默认中通模板(6分区×6阶梯) | [sqlite_rule_repository.cpp](file:///Users/cxd/duckdb/xiaoqiao_freight/src/db/sqlite_rule_repository.cpp#L228-L328) | `CreateDefaultTemplate()` |
| 6 个重量阶梯定义列表 | [sqlite_rule_repository.cpp](file:///Users/cxd/duckdb/xiaoqiao_freight/src/db/sqlite_rule_repository.cpp#L299-L306) | `tiers` 局部变量 |
| 6 个分区定义 | [sqlite_rule_repository.cpp](file:///Users/cxd/duckdb/xiaoqiao_freight/src/db/sqlite_rule_repository.cpp#L263-L276) | `zones` 局部变量 |
| 新增客户时自动创建专属报价 | [sqlite_rule_repository.cpp](file:///Users/cxd/duckdb/xiaoqiao_freight/src/db/sqlite_rule_repository.cpp#L718-L820) | `AddCustomer()` |
| 单条运费计算 SQL | [calc_service.cpp](file:///Users/cxd/duckdb/xiaoqiao_freight/src/services/calc_service.cpp#L33-L157) | `CalcSingle()` |
| 10 层 CTE 批量计算 SQL | [calc_service.cpp](file:///Users/cxd/duckdb/xiaoqiao_freight/src/services/calc_service.cpp#L240-L413) | `BuildCalcSQL()` |
| 两轮表头自动映射 | [calc_service.cpp](file:///Users/cxd/duckdb/xiaoqiao_freight/src/services/calc_service.cpp#L450-L498) | `AutoMapColumns()` |
| TRY_CAST + COALESCE 创建标准化表 | [calc_service.cpp](file:///Users/cxd/duckdb/xiaoqiao_freight/src/services/calc_service.cpp#L500-L529) | `CreateNormalizedTable()` |
| 历史记录 CRUD | [history_service.cpp](file:///Users/cxd/duckdb/xiaoqiao_freight/src/services/history_service.cpp) | `AddHistory/QueryHistory/DeleteHistory/CleanupOldData` |
| 主窗口布局 + QSS 样式 | [main_window.cpp](file:///Users/cxd/duckdb/xiaoqiao_freight/src/ui/main_window.cpp#L31-L229) | `SetupUI()/SetupStyles()` |
| 页脚官网链接（外链+下划线） | [main_window.cpp](file:///Users/cxd/duckdb/xiaoqiao_freight/src/ui/main_window.cpp#L133-L142) | footer_label_ |
| 授权启动后检查弹窗 | [main_window.cpp](file:///Users/cxd/duckdb/xiaoqiao_freight/src/ui/main_window.cpp#L300-L313) | `CheckLicenseStartup()` |
| macdeployqt + 签名修复脚本 | [deploy_mac.sh](file:///Users/cxd/duckdb/xiaoqiao_freight/scripts/deploy_mac.sh) | 第 1-46 行 |

---

*文档版本：1.1*  
*最后更新：2026-07-24*  
*版权所有 © 2026 杭州喵喵至家网络有限公司 www.hbdxm.com*
