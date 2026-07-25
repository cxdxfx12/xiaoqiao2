-- =============================================================
-- 客户级计费参数覆写 + 拉均重合同 —— SQLite 回滚脚本（R3 保险）
-- 文档：docs/DESIGN_客户级覆写与拉均重合同.md
-- 适用场景：新功能上线后发现价格异常，需要立刻退回 v10 行为；
--           或者 AppConfig feature_customer_override=false 后，
--           想把库里的新列清空（不是必须，不清也行；脚本提供两种模式）。
-- =============================================================
--
-- 用法：
--   sqlite3 /path/to/rules.db < docs/sql/rollback_customer_override_v15.sql
--
-- SQLite 版本要求：>= 3.35.0（DROP COLUMN 需要；低于此版本只能走"模式一：清空"）。
-- 可先运行：SELECT sqlite_version();  确认版本。
-- =============================================================

-- -------- 模式一：温和降级（推荐先跑这个）--------
-- 不 DROP 列，只把所有客户级/模板级新字段复位为"不启用/回落模板"的默认值。
-- 配合 AppConfig feature_customer_override=false 使用，行为等价于上线前。
UPDATE freight_templates
SET tpl_avg_weight_enabled    = 0,
    tpl_min_avg_kg_per_piece  = 1.0,
    tpl_avg_default_pieces    = 1
WHERE 1=1;

UPDATE customers
SET cust_rounding_mode         = '',
    cust_additional_unit       = 0,
    cust_vol_divisor           = 0,
    avg_weight_tpl_id          = NULL,
    cust_contract_no           = ''
WHERE 1=1;

-- 另：若 v1.0 老库存在过 cust_avg_enabled / cust_min_avg_kg_per_piece / cust_avg_default_pieces
-- （v1.1 已删除，不再使用），可手动取消注释清字段（若存在）：
-- UPDATE customers
-- SET cust_avg_enabled = 0,
--     cust_min_avg_kg_per_piece = 0,
--     cust_avg_default_pieces = 0
-- WHERE 1=1;

-- 把 user_version 打回 10（下次启动时不会再跑 v11→v15 的 ALTER 升级路径）
PRAGMA user_version = 10;

-- -------- v1.1 新增：两张独立拉均重合同表（avg_weight_templates / avg_weight_zones）--------
-- 温和降级（推荐先跑）：置 is_active=0 不参与计算；不清空数据，未来可重启用。
UPDATE customers SET avg_weight_tpl_id = NULL WHERE 1=1;
UPDATE avg_weight_templates SET is_active = 0 WHERE 1=1;

-- 物理删除（仅当确认以后永远不用拉均重合同功能再跑）
-- 先备份 rules.db！
-- BEGIN;
-- DROP TABLE IF EXISTS avg_weight_zones;
-- DROP TABLE IF EXISTS avg_weight_templates;
-- COMMIT;

-- -------- 模式二：物理删列（仅 SQLite >= 3.35，确定彻底废弃本功能再跑）--------
-- 先手动注释去掉下面的 BEGIN / COMMIT，再运行。
-- 注意：删列不可恢复！请先备份 rules.db 为 rules.db.bak-2026-07-26。
--
-- BEGIN;
-- ALTER TABLE customers DROP COLUMN cust_contract_no;
-- ALTER TABLE customers DROP COLUMN cust_avg_default_pieces;
-- ALTER TABLE customers DROP COLUMN cust_min_avg_kg_per_piece;
-- ALTER TABLE customers DROP COLUMN cust_avg_enabled;
-- ALTER TABLE customers DROP COLUMN cust_vol_divisor;
-- ALTER TABLE customers DROP COLUMN cust_additional_unit;
-- ALTER TABLE customers DROP COLUMN cust_rounding_mode;
-- ALTER TABLE freight_templates DROP COLUMN tpl_avg_default_pieces;
-- ALTER TABLE freight_templates DROP COLUMN tpl_min_avg_kg_per_piece;
-- ALTER TABLE freight_templates DROP COLUMN tpl_avg_weight_enabled;
-- PRAGMA user_version = 10;
-- COMMIT;
--
-- VACUUM;   -- 可选：回收已删列占用的磁盘空间（需要独占库，且 sqlite3 命令行下运行）
