#!/bin/bash
# ─────────────────────────────────────────────────────────
#  Mini-UnionFS  –  Automated Test Suite (FIXED)
# ─────────────────────────────────────────────────────────

FUSE_BINARY="./mini_unionfs"
TEST_DIR="./unionfs_test_env"
LOWER_DIR="$TEST_DIR/lower"
UPPER_DIR="$TEST_DIR/upper"
MOUNT_DIR="$TEST_DIR/mnt"

GREEN='\033[0;32m'
RED='\033[0;31m'
NC='\033[0m'

PASS=0
FAIL=0

run_test() {
    local name="$1"
    local result="$2"
    echo -n "  $name ... "
    if [ "$result" = "1" ]; then
        echo -e "${GREEN}PASSED${NC}"
        ((PASS++))
    else
        echo -e "${RED}FAILED${NC}"
        ((FAIL++))
    fi
}

echo ""
echo "========================================="
echo "  Mini-UnionFS Test Suite"
echo "========================================="

# ── Setup ─────────────────────────────────────
echo ""
echo "[Setup] Creating test environment..."
rm -rf "$TEST_DIR"
mkdir -p "$LOWER_DIR" "$UPPER_DIR" "$MOUNT_DIR"

echo "base_only_content"   > "$LOWER_DIR/base.txt"
echo "to_be_deleted"       > "$LOWER_DIR/delete_me.txt"
echo "lower_data"          > "$LOWER_DIR/shared.txt"
echo "upper_data"          > "$UPPER_DIR/shared.txt"

# ── Mount ─────────────────────────────────────
echo "[Setup] Mounting Mini-UnionFS..."
"$FUSE_BINARY" "$LOWER_DIR" "$UPPER_DIR" "$MOUNT_DIR" -f &
FUSE_PID=$!
sleep 1

if ! mountpoint -q "$MOUNT_DIR"; then
    echo -e "${RED}ERROR: Mount failed. Is FUSE installed? Try: sudo apt install fuse libfuse-dev${NC}"
    exit 1
fi
echo "[Setup] Mounted successfully (PID $FUSE_PID)"
echo ""

# ── Tests ─────────────────────────────────────
echo "--- Test 1: Layer Visibility ---"
res=$(grep -c "base_only_content" "$MOUNT_DIR/base.txt" 2>/dev/null)
run_test "Lower layer file is visible in mount" "$([ "$res" -eq 1 ] && echo 1 || echo 0)"

echo ""
echo "--- Test 2: Upper Layer Precedence ---"
res=$(grep -c "upper_data" "$MOUNT_DIR/shared.txt" 2>/dev/null)
run_test "Upper layer overrides lower for shared file" "$([ "$res" -eq 1 ] && echo 1 || echo 0)"

echo ""
echo "--- Test 3: Copy-on-Write ---"
echo "modified_content" >> "$MOUNT_DIR/base.txt" 2>/dev/null
sleep 0.2

# ✅ FIXED SAFE COUNTING
in_mount=$(grep -c "modified_content" "$MOUNT_DIR/base.txt" 2>/dev/null | tr -d '\n')
[ -z "$in_mount" ] && in_mount=0

in_upper=$(grep -c "modified_content" "$UPPER_DIR/base.txt" 2>/dev/null | tr -d '\n')
[ -z "$in_upper" ] && in_upper=0

in_lower=$(grep -c "modified_content" "$LOWER_DIR/base.txt" 2>/dev/null | tr -d '\n')
[ -z "$in_lower" ] && in_lower=0

run_test "Modified content visible in mount"         "$([ "$in_mount" -ge 1 ] && echo 1 || echo 0)"
run_test "Copy created in upper layer"               "$([ "$in_upper" -ge 1 ] && echo 1 || echo 0)"
run_test "Original lower layer file is untouched"   "$([ "$in_lower" -eq 0 ] && echo 1 || echo 0)"

echo ""
echo "--- Test 4: Whiteout (Deletion) ---"
rm "$MOUNT_DIR/delete_me.txt" 2>/dev/null
sleep 0.2
visible=$([ ! -f "$MOUNT_DIR/delete_me.txt" ] && echo 1 || echo 0)
lower_intact=$([ -f "$LOWER_DIR/delete_me.txt" ] && echo 1 || echo 0)
whiteout_exists=$([ -f "$UPPER_DIR/.wh.delete_me.txt" ] && echo 1 || echo 0)

run_test "Deleted file no longer visible in mount"   "$visible"
run_test "Original file still exists in lower layer" "$lower_intact"
run_test "Whiteout marker created in upper layer"    "$whiteout_exists"

echo ""
echo "--- Test 5: Create New File ---"
echo "new content" > "$MOUNT_DIR/newfile.txt" 2>/dev/null
sleep 0.2

in_mount=$([ -f "$MOUNT_DIR/newfile.txt" ] && echo 1 || echo 0)
in_upper=$([ -f "$UPPER_DIR/newfile.txt" ] && echo 1 || echo 0)
in_lower=$([ ! -f "$LOWER_DIR/newfile.txt" ] && echo 1 || echo 0)

run_test "New file visible in mount"                 "$in_mount"
run_test "New file stored in upper layer"            "$in_upper"
run_test "New file NOT in lower layer"               "$in_lower"

echo ""
echo "--- Test 6: Directory Operations ---"
mkdir "$MOUNT_DIR/new_dir" 2>/dev/null
sleep 0.2
dir_in_upper=$([ -d "$UPPER_DIR/new_dir" ] && echo 1 || echo 0)
dir_in_mount=$([ -d "$MOUNT_DIR/new_dir" ] && echo 1 || echo 0)

run_test "New directory created in upper layer"     "$dir_in_upper"
run_test "New directory visible in mount"           "$dir_in_mount"

rmdir "$MOUNT_DIR/new_dir" 2>/dev/null
sleep 0.2
dir_gone=$([ ! -d "$MOUNT_DIR/new_dir" ] && echo 1 || echo 0)
run_test "Directory successfully removed"           "$dir_gone"

# ── Teardown ──────────────────────────────────
echo ""
echo "[Teardown] Unmounting..."
fusermount -u "$MOUNT_DIR" 2>/dev/null || umount "$MOUNT_DIR" 2>/dev/null
wait $FUSE_PID 2>/dev/null
rm -rf "$TEST_DIR"

# ── Summary ───────────────────────────────────
echo ""
echo "========================================="
echo -e "  Results: ${GREEN}$PASS passed${NC}  /  ${RED}$FAIL failed${NC}"
echo "========================================="
echo ""