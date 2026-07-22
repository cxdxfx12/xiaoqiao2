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
   - 4.5 [UI界面](#45-ui界面)
5. [数据库设计](#5-数据库设计)
   - 5.1 [SQLite 规则库](#51-sqlite-规则库)
   - 5.2 [DuckDB 计算引擎](#52-duckdb-计算引擎)
6. [运费计算逻辑详解](#6-运费计算逻辑详解)
7. [踩过的坑与经验教训](#7-踩过的坑与经验教训)
8. [部署与打包](#8-部署与打包)

---

## 1. 项目概述

**小乔运费结算**是一款基于 Qt6 + DuckDB 开发的桌面运费计算软件，主要功能包括：

- **单条计算**：手动输入省份、重量等信息，快速计算运费
- **批量计算**：支持 Excel/CSV 文件批量导入，一键计算百万级数据
- **对比分析**：多模板价格对比，辅助决策
- **历史记录**：计算任务历史管理，支持结果导出
- **规则配置**：灵活的运费模板、分区定价、附加费策略配置
- **客户管理**：客户级别的默认模板绑定和折扣设置

### 核心特性

- ⚡ **高性能**：基于 DuckDB 列式存储引擎，百万级数据秒级计算
- 📊 **批量处理**：支持 Excel/CSV/Parquet 多种格式导入导出
- 🎯 **智能表头映射**：自动识别中英文表头，支持手动映射
- 🔧 **灵活规则**：6 级重量阶梯、燃油附加费、地区加价、自定义策略
- 🖥️ **自动性能调优**：自动检测系统资源，使用 90% 性能计算

---

## 2. 技术栈

| 类别 | 技术 | 版本/说明 |
|------|------|-----------|
| 语言 | C++ | C++17 |
| GUI 框架 | Qt6 | 6.5+（Widgets） |
| 构建系统 | CMake | 3.20+ |
| 规则数据库 | SQLite | 通过 Qt QSqlDatabase |
| 计算引擎 | DuckDB | 1.5.4 |
| 打包工具 | macdeployqt | Qt 自带 |
| 代码签名 | codesign | macOS 系统工具 |

---

## 3. 项目结构

```
xiaoqiao_freight/
├── CMakeLists.txt              # CMake 构建配置
├── resources/
│   └── resources.qrc           # Qt 资源文件（图标等）
├── scripts/
│   └── deploy_mac.sh           # macOS 部署脚本（自动签名修复）
└── src/
    ├── main.cpp                # 程序入口
    ├── core/                   # 核心类型与配置
    │   ├── app_config.hpp      # 应用配置（单例）
    │   ├── app_config.cpp
    │   └── freight_types.hpp   # 数据结构定义
    ├── db/                     # 数据存储层
    │   ├── sqlite_rule_repository.hpp  # SQLite 规则库
    │   ├── sqlite_rule_repository.cpp
    │   ├── duckdb_manager.hpp         # DuckDB 管理器
    │   └── duckdb_manager.cpp
    ├── services/               # 业务服务层
    │   ├── calc_service.hpp           # 计算服务
    │   ├── calc_service.cpp
    │   ├── rule_service.hpp           # 规则服务
    │   ├── rule_service.cpp
    │   ├── history_service.hpp        # 历史记录服务
    │   └── history_service.cpp
    └── ui/                     # 界面层
        ├── main_window.hpp     # 主窗口
        ├── main_window.cpp
        ├── icon_manager.hpp    # 图标管理器
        ├── icon_manager.cpp
        └── dialogs/            # 对话框
            ├── single_calc_dialog.*     # 单条计算
            ├── batch_calc_dialog.*      # 批量计算
            ├── compare_dialog.*         # 对比分析
            ├── history_dialog.*         # 历史记录
            ├── rule_setting_dialog.*    # 规则设置
            ├── template_edit_dialog.*   # 模板编辑
            ├── customer_setting_dialog.*# 客户设置
            ├── system_setting_dialog.*  # 系统设置
            ├── header_mapping_dialog.*  # 表头映射
            └── about_dialog.*           # 关于
```

### 架构设计

采用经典的三层架构：

```
UI 层 (Qt Widgets)
    ↓
Service 层 (业务逻辑)
    ├── CalcService     # 计算服务
    ├── RuleService     # 规则服务
    └── HistoryService  # 历史服务
    ↓
DB 层 (数据持久化)
    ├── SQLite      # 规则数据（持久化）
    └── DuckDB      # 计算引擎（内存/文件）
```

---

## 4. 核心模块详解

### 4.1 计算引擎

**文件**：[calc_service.cpp](src/services/calc_service.cpp)

计算引擎是整个系统的核心，基于 DuckDB 的向量化执行引擎实现高性能批量计算。

#### 核心方法

| 方法 | 说明 |
|------|------|
| `CalcSingle()` | 单条运费计算 |
| `CalcBatch()` | 批量计算（输入 DuckDB 表名） |
| `CalcFromFile()` | 从文件计算（自动导入→计算→导出） |
| `BuildCalcSQL()` | 构建计算 SQL（核心） |

#### 计算 SQL 结构（CTE 链式调用）

批量计算使用一条完整的 SQL 完成，采用 10 层 CTE（公共表表达式）逐层计算：

```sql
CREATE OR REPLACE TABLE output_table AS
WITH
input_data AS (
    -- 1. 输入数据清洗：COALESCE 处理空值，计算计费重量
),
customer_template_lookup AS (
    -- 2. 客户模板自动绑定：LEFT JOIN customers 表
    --    根据 customer_id 获取客户的 default_template
),
template_info AS (
    -- 3. 模板信息关联：获取 default_no_weight_fee 等
),
matched_zone AS (
    -- 4. 分区匹配：根据目的省份匹配分区组
    --    省份名归一化：去掉"省/市/自治区"等后缀
),
matched_tier AS (
    -- 5. 重量阶梯匹配：左开右闭 (min_weight, max_weight]
),
tier_max AS (
    -- 6. 最大阶梯兜底：超过最高阶梯时使用
),
base_fee_calc AS (
    -- 7. 基础运费计算：阶梯定价公式
),
fuel_surcharge_calc AS (
    -- 8. 燃油附加费计算：取最新生效日期的费率
),
remote_area_calc AS (
    -- 9. 地区加价计算：省/市/区三级粒度匹配
),
strategy_surcharge_calc AS (
    -- 10. 其他附加费策略：SUM 聚合所有匹配的策略
)
SELECT ... FROM strategy_surcharge_calc  -- 最终输出
```

#### 计费重量计算

```
计费重量 = max(实际重量, 体积重量)  （体积重量 > 0 时才比较）
```

体积重量通常由外部计算后传入，公式一般为：`体积(cm³) / 6000`

---

### 4.2 规则系统

**文件**：[sqlite_rule_repository.cpp](src/db/sqlite_rule_repository.cpp)

规则系统负责管理运费模板、分区、阶梯定价、附加费等配置数据，存储在 SQLite 中。

#### 运费模板 (freight_templates)

每个模板代表一套完整的报价体系，包含：

- 基本信息：模板ID、名称、快递公司
- 体积重系数：默认 6000
- 无重量默认运费：重量缺失或为 0 时的兜底费用
- 是否默认模板

#### 分区组 (zone_groups + zone_group_provinces)

- 每个模板包含多个分区（如一区、二区...）
- 每个分区包含若干省份
- 同一价格区的省份共享同一套阶梯定价

**默认分区（中通标准）**：

| 分区 | 省份 |
|------|------|
| 一区 | 江苏、浙江、安徽、上海（4个） |
| 二区 | 山东、广东、福建、北京、河南、湖北、湖南、江西、天津、河北（10个） |
| 三区 | 山西、广西、四川、重庆、陕西、贵州、辽宁、吉林、黑龙江、云南（10个） |
| 四区 | 海南、甘肃、青海、内蒙古、宁夏（5个） |
| 五区-新疆 | 新疆 |
| 五区-西藏 | 西藏 |

#### 阶梯定价 (tiered_pricing)

每个分区下定义 6 个重量阶梯：

| 阶梯代码 | 重量范围 | 计价方式 |
|----------|----------|----------|
| tier_0_0.5 | 0 - 0.5 KG | 一口价（first_price） |
| tier_0.5_1 | 0.5 - 1 KG | 一口价（first_price） |
| tier_1_2 | 1 - 2 KG | 一口价（first_price） |
| tier_2_3 | 2 - 3 KG | 一口价（first_price） |
| tier_3_30 | 3 - 30 KG | 首重 + 计费重量 × 续重价 |
| tier_30_plus | 30 KG 以上 | 首重 + 计费重量 × 续重价 |

> **重要**：前 4 个阶梯（0-3KG）是一口价，`additional_price` 必须为 0，否则会重复计算。

#### 燃油附加费 (fuel_surcharge)

- 按模板设置，支持多条记录（不同生效日期）
- 取 **小于等于当前日期** 的最新一条生效记录
- 费率为百分比（如 0.05 表示 5%）
- 计算方式：`基础运费 × 费率`
- 有 `is_active` 字段控制启用/停用

#### 地区加价 (remote_areas)

- 按模板设置，支持省/市/区三级粒度
- 每条记录设置附加费金额
- 有 `is_active` 字段控制启用/停用

#### 自定义附加费策略 (surcharge_strategies)

灵活的加价策略系统，支持：

**作用范围 (strategy_scope)**：
- `global`：全局生效
- `template`：指定模板生效
- `province`：指定省份生效
- `customer`：指定客户生效

**策略类型 (strategy_type)**：
- `fixed`：固定金额
- `percentage`：基础运费百分比
- `per_weight`：按重量计费（每公斤多少钱）

**其他属性**：
- `priority`：优先级（仅排序用，金额是 SUM 累加）
- `min_weight / max_weight`：重量条件过滤
- `is_active`：启用/停用开关

---

### 4.3 数据存储层

#### 双数据库架构

| 数据库 | 用途 | 特点 |
|--------|------|------|
| SQLite | 规则数据持久化 | 事务性、稳定、WAL 模式 |
| DuckDB | 批量计算引擎 | 列式存储、向量化执行、高性能 |

**数据流**：

```
应用启动
  ↓
SQLite (rules.db) ──→ 读取规则 ──→ DuckDB（内存表）
                                         ↓
                               批量计算 / 文件导入导出
                                         ↓
                               结果表 ──→ 导出 Excel/CSV
```

每次启动应用时，都会从 SQLite 重新加载规则到 DuckDB，确保数据一致性。

#### DuckDB 管理器

**文件**：[duckdb_manager.cpp](src/db/duckdb_manager.cpp)

核心功能：
- `Init()`：初始化 DuckDB，设置性能参数，加载 Excel 扩展
- `LoadRulesFromSQLite()`：从 SQLite 同步规则到 DuckDB
- `ImportFromFile()`：导入 CSV/Excel/Parquet 文件
- `ExportToFile()`：导出结果到文件
- `CreateConnection()`：创建新连接

**性能配置**（启动时自动设置）：
```sql
SET memory_limit = 'XMB';   -- 系统内存的 90%
SET threads = N;            -- CPU 核心数的 90%
```

---

### 4.4 表头映射机制

**文件**：[calc_service.cpp](src/services/calc_service.cpp) 中的 `AutoMapColumns()`

批量计算时，用户导入的 Excel 表头千变万化，需要自动映射到系统的标准列名。

#### 标准列名

| 标准列名 | 说明 | 必填 |
|----------|------|------|
| order_id | 订单号 | 否 |
| customer_id | 客户编号 | 否 |
| dest_province | 目的省份 | **是** |
| dest_city | 目的城市 | 否 |
| weight | 实际重量 | **是** |
| vol_weight | 体积重量 | 否 |

#### 映射策略（两轮匹配）

**第 1 轮：精确匹配**（不区分大小写）
- 完全相等才算匹配
- 避免 "订单客户" 误匹配到 "客户" 这类问题

**第 2 轮：子串匹配**（仅对未匹配的列）
- 表头包含关键字就算匹配
- 优先级低于精确匹配

#### 映射关键字列表

```cpp
{"order_id",     {"order_id", "order_no", "waybill", "订单号", "订单编号", "运单号", "快递单号", "单号"}},
{"dest_province",{"dest_province", "to_province", "province", "目的省份", "省份", "收件省份", "到达省份", "收货省份"}},
{"dest_city",    {"dest_city", "to_city", "city", "目的城市", "城市", "收件城市", "到达城市", "收货城市"}},
{"weight",       {"weight", "actual_weight", "gross_weight", "real_weight", "结算重量", "重量", "实际重量", "实重", "毛重", "计费重量"}},
{"vol_weight",   {"vol_weight", "volume_weight", "volumetric_weight", "体积重量", "体积重", "体积", "抛重"}},
{"customer_id",  {"customer_id", "cust_id", "customer", "客户id", "客户编号", "客户", "客户名称", "客户名"}},
```

#### 省份名归一化

匹配分区时，会自动去掉省份名的后缀，确保匹配成功：

```sql
REGEXP_REPLACE(dest_province, '(省|市|维吾尔自治区|回族自治区|壮族自治区|自治区)$', '')
```

例如：
- "浙江省" → "浙江"
- "上海市" → "上海"
- "新疆维吾尔自治区" → "新疆"
- "广西壮族自治区" → "广西"

---

### 4.5 UI 界面

#### 主窗口 (MainWindow)

- 顶部广告轮播（4 条，4 秒切换）
- 中部 4 个大卡片按钮：单条计算、批量计算、对比分析、历史记录
- 底部设置按钮：规则设置、客户规则设置、系统设置、关于
- 页脚：公司名 + 版权 + 网址链接

#### 批量计算对话框 (BatchCalcDialog)

使用 QStackedWidget 两页式设计：
- **第 1 页**：文件选择、拖拽上传、文件信息
- **第 2 页**：计算进度、结果预览（前100条）、导出按钮

#### 表头映射对话框 (HeaderMappingDialog)

当必填列（dest_province / weight）缺失时弹出：
- 左侧：导入文件的表头
- 右侧：系统标准列名（红色 * 标必填）
- 红色箭头连线显示映射关系
- 支持点击交互调整映射
- 底部预览前 5 行数据

#### 规则设置对话框 (RuleSettingDialog)

左侧模板列表，右侧 Tab 页：
- 基本信息
- 分区报价（矩阵式表格）
- 燃油附加费
- 地区加价
- 加价策略

---

## 5. 数据库设计

### 5.1 SQLite 规则库

**文件位置**：`AppDataLocation/rules.db`

#### Schema 版本管理

通过 `schema_version` 表控制数据库升级，当前版本：**11**

版本历史：
- v10：大版本升级，重建所有表，移除部分 UNIQUE 约束
- v11：新增 `freight_templates.default_no_weight_fee` 字段

#### 表清单

| 表名 | 说明 | 关键字段 |
|------|------|---------|
| customers | 客户表 | customer_id, customer_name, discount_rate, default_template |
| freight_templates | 运费模板 | template_id, template_name, carrier_name, default_no_weight_fee |
| zone_groups | 分区组 | id, template_id, group_code, group_name, sort_order |
| zone_group_provinces | 分区省份关联 | id, template_id, group_code, province |
| tiered_pricing | 阶梯定价 | id, template_id, group_code, tier_code, min_weight, max_weight, first_price, additional_price |
| fuel_surcharge | 燃油附加费 | id, template_id, effective_date, rate, is_active |
| remote_areas | 地区加价 | id, template_id, province, city, district, surcharge, is_active |
| surcharge_strategies | 附加费策略 | strategy_id, strategy_name, strategy_scope, strategy_type, amount, priority, is_active |
| surcharge_provinces | 策略-省份关联 | strategy_id, province |
| surcharge_customers | 策略-客户关联 | strategy_id, customer_id |
| surcharge_date_ranges | 策略-日期范围 | strategy_id, start_date, end_date, week_days |
| schema_version | Schema版本 | version |

> **注意**：`fuel_surcharge` 和 `remote_areas` 没有 `UNIQUE(template_id, ...)` 约束，允许重复记录（但会导致 LEFT JOIN 行倍增，需注意数据清理）。

### 5.2 DuckDB 计算引擎

DuckDB 中的表是从 SQLite 同步过来的，结构基本一致，但没有主键约束和自增ID。

**特殊功能**：
- 加载 `excel` 扩展，支持 `read_xlsx()` 函数读取 Excel
- 支持 `COPY ... TO ... (FORMAT xlsx, HEADER TRUE)` 导出 Excel
- 内存限制和线程数自动配置

---

## 6. 运费计算逻辑详解

### 6.1 计算公式

总运费 = 基础运费 + 燃油附加费 + 地区加价 + 其他附加费

#### 基础运费计算规则

| 重量范围 | 计算公式 | 说明 |
|----------|----------|------|
| 重量 = 0 或 NULL | default_no_weight_fee | 无重量兜底费用 |
| 未匹配到分区 | 0 | 找不到对应省份 |
| 0-3KG | first_price（一口价） | 4 个阶梯都是一口价 |
| 3-30KG | first_price + charge_weight × additional_price | 首重 + 续重 |
| 30KG+ | first_price + charge_weight × additional_price | 首重 + 续重 |
| 超过最高阶梯 | tier_max.first_price + charge_weight × tier_max.additional_price | 兜底逻辑 |

> **重要边界**：重量匹配采用 **左开右闭** 原则：`charge_weight > min_weight AND charge_weight <= max_weight`

#### 燃油附加费

```
燃油附加费 = 基础运费 × 燃油费率
```

- 取小于等于当前日期的最新一条生效记录
- 必须 `is_active = 1`

#### 地区加价

```
地区加价 = Σ 所有匹配的地区加价记录的 surcharge
```

- 支持省/市/区三级粒度匹配
- 省份匹配时会自动去掉"省/市/自治区"等后缀
- 多条匹配时金额累加
- 必须 `is_active = 1`

**匹配规则**：
1. 只设置了省份 → 全省加价
2. 设置了省份 + 城市 → 该市加价
3. 只设置了城市（省份为空）→ 全国该城市加价
4. 设置了省份 + 城市 + 区县 → 该区加价

#### 其他附加费

所有匹配的策略金额累加（SUM）：

```
策略附加费 = Σ 各匹配策略的金额
```

策略匹配条件：
1. `is_active = 1`
2. 作用范围匹配（全局 / 同模板 / 同省份 / 同客户）
3. 重量范围匹配（min_weight / max_weight）

---

## 7. 踩过的坑与经验教训

### 7.1 DuckDB 相关

#### ❌ 坑1：MaterializedQueryResult::GetValue<T>() 类型转换 Bug

**现象**：通过 `res->GetValue<T>(col, row)` 获取值时，小数被截断，类型判断错误。

**原因**：DuckDB 1.5.4 的 C++ API 模板方法有 bug。

**解决**：改用 `res->GetValue(col, row).GetValue<T>()`，先拿 Value 对象再转换。

```cpp
// 错误写法
int64_t val = result->GetValue<int64_t>(0, 0);

// 正确写法
int64_t val = result->GetValue(0, 0).GetValue<int64_t>();
```

---

#### ❌ 坑2：read_excel 函数不存在

**现象**：导入 Excel 时报函数不存在的错误。

**原因**：DuckDB 1.5 的 Excel 函数是 `read_xlsx()`（在 excel 扩展中），不是 `read_excel()`。

**解决**：
```sql
INSTALL excel;
LOAD excel;
SELECT * FROM read_xlsx('file.xlsx', header=true);
```

---

#### ❌ 坑3：information_schema.columns 不稳定

**现象**：用 `information_schema.columns` 查询列名，有时返回空。

**原因**：DuckDB 对 information_schema 的支持不完善。

**解决**：改用 `DESCRIBE SELECT * FROM table` 获取列信息。

```cpp
// 错误
SELECT column_name FROM information_schema.columns WHERE table_name = 'xxx';

// 正确
DESCRIBE SELECT * FROM xxx;
```

---

#### ❌ 坑4：空字符串转数值崩溃

**现象**：体积重量列为空字符串时，CAST 报错导致计算失败。

**原因**：`CAST('' AS DOUBLE)` 会抛出异常。

**解决**：使用 `TRY_CAST` 代替 `CAST`，转换失败返回 NULL，再用 COALESCE 兜底。

```sql
-- 错误
COALESCE(CAST(weight AS DOUBLE), 0)

-- 正确
COALESCE(TRY_CAST(weight AS DOUBLE), 0)
```

---

### 7.2 计算逻辑相关

#### ❌ 坑5：一口价阶梯的 additional_price 设置错误

**现象**：2KG 运费算出来是 7.12 元，正确应该是 3.56 元。

**原因**：前 4 个一口价阶梯（0-0.5, 0.5-1, 1-2, 2-3KG）的 `additional_price` 被错误地设成了和 `first_price` 一样的值，导致计算公式变成 `first_price + charge_weight × additional_price`，双重收费。

**解决**：前 4 个阶梯的 `additional_price` 必须设为 0，因为它们是一口价，只收 first_price。

```cpp
// 修复：强制把前4个阶梯的 additional_price 设为0
fix_q.exec("UPDATE tiered_pricing SET additional_price = 0 WHERE tier_code IN ('tier_0_0.5', 'tier_0.5_1', 'tier_1_2', 'tier_2_3')");
```

---

#### ❌ 坑6：燃油附加费重复记录导致行倍增

**现象**：计算结果运费翻倍，行数异常增多。

**原因**：`fuel_surcharge` 表中有多条相同 template_id + effective_date 的记录，LEFT JOIN 时每行都匹配多次，导致行倍增。

**解决**：
1. 移除 `UNIQUE(template_id, effective_date)` 约束（允许新增）
2. 但要注意数据清理，避免重复
3. 或者用子查询取 MAX(id) / DISTINCT 去重

---

#### ❌ 坑7：customer_id 子串匹配误伤

**现象**："订单客户" 列被误匹配为 customer_id。

**原因**：表头映射时，"订单客户" 包含 "客户" 关键字，子串匹配命中。

**解决**：分两轮匹配，先精确匹配，再子串匹配。精确匹配优先级更高。

---

### 7.3 SQLite 相关

#### ❌ 坑8：DROP TABLE 后约束残留

**现象**：在 WAL 模式下，C++ 执行 DROP TABLE 后重建，旧的 UNIQUE 约束仍然生效。

**原因**：SQLite WAL 模式下的 schema 缓存问题。

**解决**：直接用 sqlite3 命令行工具重建表更可靠，或者关闭 WAL 后再操作。

---

### 7.4 打包部署相关

#### ❌ 坑9：两套 Qt 库冲突

**现象**：应用启动崩溃，提示 "could not load the Qt platform plugin cocoa"，控制台输出两个 QtCore 地址。

**原因**：应用同时加载了系统 homebrew 的 Qt 和应用包内的 Qt，两套库冲突。

**解决**：用 `macdeployqt` 正确部署 Qt 框架到 .app 包内，确保依赖路径都是 `@executable_path/../Frameworks`。

---

#### ❌ 坑10：第三方库签名冲突

**现象**：macdeployqt 报 codesign 验证错误，提示 `libbrotlicommon.1.dylib` 签名无效。

**原因**：第三方 dylib（来自 homebrew）已经有签名，和 macdeployqt 的 ad-hoc 签名冲突。

**解决**：
1. 运行 macdeployqt 之后
2. 先移除所有 dylib 的旧签名：`codesign --remove-signature`
3. 再重新 ad-hoc 签名整个 app：`codesign --force --deep -s - app.app`

已封装成自动化脚本：[deploy_mac.sh](scripts/deploy_mac.sh)

---

#### ❌ 坑11：隔离属性导致打不开

**现象**：下载或拷贝的应用提示 "无法打开，因为无法验证开发者"。

**原因**：macOS 的 Gatekeeper 隔离属性（com.apple.quarantine）。

**解决**：移除隔离属性
```bash
xattr -dr com.apple.quarantine xiaoqiao_freight.app
```

---

### 7.5 数据导入相关

#### ❌ 坑12：SQLite ATTACH 到 DuckDB 问题多

**现象**：尝试用 `ATTACH 'rules.db' AS sqlite_rules (TYPE SQLITE)` 直接读取，类型转换各种问题。

**原因**：DECIMAL 类型在两个数据库中表示不一致，导致数据错乱。

**解决**：用 Qt 的 QSqlQuery 从 SQLite 读出来，手动拼接 INSERT 语句插入到 DuckDB。虽然慢一点，但数据准确。

---

#### ❌ 坑13：导出文件没有表头

**现象**：导出的 Excel 文件第一行就是数据，没有列名。

**原因**：DuckDB 的 COPY 命令默认不带表头。

**解决**：加上 `HEADER TRUE` 参数。

```sql
COPY table TO 'file.xlsx' (FORMAT xlsx, HEADER TRUE);
```

---

## 8. 部署与打包

### 8.1 编译

```bash
mkdir build && cd build
cmake ..
make -j4
```

编译产物：`build/bin/xiaoqiao_freight.app`

### 8.2 自动部署

已集成到 CMake 的 POST_BUILD 步骤，每次编译后自动执行：

1. 运行 `macdeployqt` 部署 Qt 框架
2. 移除第三方 dylib 的旧签名
3. 重新 ad-hoc 签名整个应用
4. 移除隔离属性

脚本位置：[scripts/deploy_mac.sh](scripts/deploy_mac.sh)

### 8.3 手动部署

如果需要手动重新部署：

```bash
bash scripts/deploy_mac.sh build/bin/xiaoqiao_freight.app
```

### 8.4 制作 DMG

使用 `hdiutil` 或第三方工具制作 DMG 安装包。

---

## 附录：关键代码位置速查

| 功能 | 文件 | 位置 |
|------|------|------|
| 计算SQL构建 | [calc_service.cpp](src/services/calc_service.cpp) | `BuildCalcSQL()` |
| 表头映射 | [calc_service.cpp](src/services/calc_service.cpp) | `AutoMapColumns()` |
| 省份归一化 | [calc_service.cpp](src/services/calc_service.cpp) | `REGEXP_REPLACE` |
| 默认数据初始化 | [sqlite_rule_repository.cpp](src/db/sqlite_rule_repository.cpp) | `CreateDefaultTemplate()` |
| DuckDB性能配置 | [duckdb_manager.cpp](src/db/duckdb_manager.cpp) | `Init()` |
| 6个重量阶梯定义 | [sqlite_rule_repository.cpp](src/db/sqlite_rule_repository.cpp) | `tiers` 列表 |
| 6个分区定义 | [sqlite_rule_repository.cpp](src/db/sqlite_rule_repository.cpp) | `zones` 列表 |
| 系统配置存储 | [app_config.hpp](src/core/app_config.hpp) | `AppConfig` 类 |

---

*文档版本：1.0*
*最后更新：2026-07-23*
