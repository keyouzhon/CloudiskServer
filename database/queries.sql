-- ============================================
-- CloudiskServer 常用查询 SQL
-- ============================================

USE Netdisk;

-- ============================================
-- 用户相关查询
-- ============================================

-- 查看所有用户
SELECT name, created_at, updated_at FROM User;

-- 查看用户数量
SELECT COUNT(*) AS user_count FROM User;

-- 查看指定用户的详细信息
SELECT * FROM User WHERE name = 'username';

-- 查看用户的登录日志
SELECT * FROM Login WHERE name = 'username' ORDER BY created_at DESC LIMIT 10;

-- ============================================
-- 文件目录相关查询
-- ============================================

-- 查看指定用户的所有文件和目录
SELECT code, procode, filename, filetype, filesize, md5sum 
FROM Directory 
WHERE belongID = 'username' 
ORDER BY procode, filename;

-- 查看指定目录下的所有文件和子目录
SELECT code, filename, filetype, filesize 
FROM Directory 
WHERE procode = 0 AND belongID = 'username';

-- 查看指定用户的根目录内容
SELECT code, filename, filetype, filesize 
FROM Directory 
WHERE procode = 0 AND belongID = 'username';

-- 查看文件的MD5值（用于秒传检查）
SELECT filename, md5sum, filesize 
FROM Directory 
WHERE md5sum = 'file_md5_value';

-- 统计用户文件数量
SELECT 
    belongID AS username,
    COUNT(*) AS total_items,
    SUM(CASE WHEN filetype = 'f' THEN 1 ELSE 0 END) AS file_count,
    SUM(CASE WHEN filetype = 'd' THEN 1 ELSE 0 END) AS dir_count,
    SUM(CASE WHEN filetype = 'f' THEN filesize ELSE 0 END) AS total_size
FROM Directory 
GROUP BY belongID;

-- ============================================
-- 日志相关查询
-- ============================================

-- 查看最近的登录日志
SELECT * FROM Login 
ORDER BY created_at DESC 
LIMIT 20;

-- 查看指定用户的操作日志
SELECT * FROM Operate 
WHERE name = 'username' 
ORDER BY created_at DESC 
LIMIT 20;

-- 统计操作类型
SELECT handle, COUNT(*) AS count 
FROM Operate 
GROUP BY handle 
ORDER BY count DESC;

-- 查看今天的操作日志
SELECT * FROM Operate 
WHERE DATE(created_at) = CURDATE() 
ORDER BY created_at DESC;

-- 查看失败的操作
SELECT * FROM Operate 
WHERE result LIKE '%False%' OR result LIKE '%false%' 
ORDER BY created_at DESC;

-- ============================================
-- 多点下载服务器相关查询
-- ============================================

-- 查看所有服务器信息
SELECT * FROM Server_info;

-- 查看指定文件MD5对应的服务器
SELECT ip, port 
FROM Server_info 
WHERE md5sum = 'file_md5_value';

-- 统计每个服务器的文件数量
SELECT ip, port, COUNT(*) AS file_count 
FROM Server_info 
GROUP BY ip, port;

-- ============================================
-- 数据统计查询
-- ============================================

-- 数据库总体统计
SELECT 
    (SELECT COUNT(*) FROM User) AS total_users,
    (SELECT COUNT(*) FROM Directory WHERE filetype = 'f') AS total_files,
    (SELECT COUNT(*) FROM Directory WHERE filetype = 'd') AS total_dirs,
    (SELECT SUM(filesize) FROM Directory WHERE filetype = 'f') AS total_storage,
    (SELECT COUNT(*) FROM Login) AS total_login_logs,
    (SELECT COUNT(*) FROM Operate) AS total_operate_logs;

-- 查看存储空间使用最多的用户
SELECT 
    belongID AS username,
    SUM(filesize) AS total_size,
    COUNT(*) AS file_count
FROM Directory 
WHERE filetype = 'f'
GROUP BY belongID 
ORDER BY total_size DESC 
LIMIT 10;

-- ============================================
-- 维护查询
-- ============================================

-- 查找孤立文件（没有对应MD5的文件记录）
SELECT * FROM Directory 
WHERE filetype = 'f' AND (md5sum IS NULL OR md5sum = '');

-- 查找重复的MD5（用于检查去重效果）
SELECT md5sum, COUNT(*) AS count 
FROM Directory 
WHERE filetype = 'f' AND md5sum IS NOT NULL 
GROUP BY md5sum 
HAVING count > 1;

-- 查看最近30天的活跃用户
SELECT name, COUNT(*) AS operation_count 
FROM Operate 
WHERE created_at >= DATE_SUB(NOW(), INTERVAL 30 DAY)
GROUP BY name 
ORDER BY operation_count DESC;

-- ============================================
-- 清理查询（谨慎使用）
-- ============================================

-- 删除30天前的登录日志（示例，实际执行前请确认）
-- DELETE FROM Login WHERE created_at < DATE_SUB(NOW(), INTERVAL 30 DAY);

-- 删除30天前的操作日志（示例，实际执行前请确认）
-- DELETE FROM Operate WHERE created_at < DATE_SUB(NOW(), INTERVAL 30 DAY);

-- ============================================
-- 表结构查看
-- ============================================

-- 查看所有表
SHOW TABLES;

-- 查看表结构
DESCRIBE User;
DESCRIBE Directory;
DESCRIBE Login;
DESCRIBE Operate;
DESCRIBE Server_info;

-- 查看表的索引
SHOW INDEX FROM Directory;
SHOW INDEX FROM User;

