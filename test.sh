
gcc -Wall -Wextra main.c -o db
if [ $? -ne 0 ]; then
    echo "Compilation failed!"
    exit 1
fi

rm -f test.db

echo "=== TEST 1: Basic Insert and Select ==="
./db test.db <<EOF
insert 1 user1 user1@gmail.com
select
.exit
EOF

echo -e "\n=== TEST 2: Persistence ==="
./db test.db <<EOF
select
.exit
EOF

echo -e "\n=== TEST 3: Edge Cases / Long String ==="
LONG_NAME="abcdefghijklmnopqrstuvwxyz1234567890"
./db test.db <<EOF
insert 2 $LONG_NAME email@gmail.com
.exit
EOF

echo -e "\n=== TEST 4: Maximum Table Capacity ==="
rm -f test.db

{
  for i in $(seq 1 1301); do
    echo "insert $i user$i user$i@gmail.com"
  done
  echo ".exit"
} | ./db test.db | grep -i "full" | head -n 5

rm -f test.db