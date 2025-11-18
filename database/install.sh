#!/bin/bash

# ============================================
# CloudiskServer 数据库安装脚本
# ============================================

# 数据库配置（根据实际情况修改）
DB_HOST="localhost"
DB_USER="root"
DB_PASS="950711"
DB_NAME="Netdisk"

echo "============================================"
echo "CloudiskServer 数据库安装脚本"
echo "============================================"
echo ""

# 检查 MySQL 是否安装
if ! command -v mysql &> /dev/null; then
    echo "错误: 未找到 MySQL 客户端，请先安装 MySQL"
    exit 1
fi

# 检查 SQL 文件是否存在
SQL_FILE="$(dirname "$0")/init.sql"
if [ ! -f "$SQL_FILE" ]; then
    echo "错误: 找不到 init.sql 文件"
    exit 1
fi

echo "数据库配置:"
echo "  主机: $DB_HOST"
echo "  用户: $DB_USER"
echo "  数据库: $DB_NAME"
echo ""

# 提示用户确认
read -p "是否继续安装？(y/n): " -n 1 -r
echo
if [[ ! $REPLY =~ ^[Yy]$ ]]; then
    echo "安装已取消"
    exit 0
fi

# 执行 SQL 脚本
echo "正在创建数据库和表..."
mysql -h"$DB_HOST" -u"$DB_USER" -p"$DB_PASS" < "$SQL_FILE"

if [ $? -eq 0 ]; then
    echo ""
    echo "============================================"
    echo "数据库安装成功！"
    echo "============================================"
    echo ""
    echo "已创建以下表:"
    echo "  - User (用户表)"
    echo "  - Directory (文件目录表)"
    echo "  - Login (登录日志表)"
    echo "  - Operate (操作日志表)"
    echo "  - Server_info (服务器信息表)"
    echo ""
    echo "可以使用以下命令验证:"
    echo "  mysql -u$DB_USER -p$DB_PASS -e 'USE $DB_NAME; SHOW TABLES;'"
else
    echo ""
    echo "============================================"
    echo "数据库安装失败！"
    echo "============================================"
    echo "请检查:"
    echo "  1. MySQL 服务是否运行"
    echo "  2. 数据库用户名和密码是否正确"
    echo "  3. 用户是否有创建数据库的权限"
    exit 1
fi

