#!/bin/bash

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
TEMPLATE_FILE="$PROJECT_ROOT/.gitcode/PULL_REQUEST_TEMPLATE.zh-CN.md"

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m'

log_info() {
    echo -e "${GREEN}[INFO]${NC} $1"
}

log_warn() {
    echo -e "${YELLOW}[WARN]${NC} $1"
}

log_error() {
    echo -e "${RED}[ERROR]${NC} $1"
}

check_git_status() {
    if ! git rev-parse --git-dir > /dev/null 2>&1; then
        log_error "当前目录不是git仓库"
        exit 1
    fi
    
    if [ -n "$(git status --porcelain)" ]; then
        log_warn "工作区有未提交的更改"
        read -p "是否继续？(y/n) " choice
        if [ "$choice" != "y" ]; then
            exit 1
        fi
    fi
}

check_remotes() {
    if ! git remote get-url origin > /dev/null 2>&1; then
        log_error "未找到origin远程仓库"
        exit 1
    fi
    
    if ! git remote get-url upstream > /dev/null 2>&1; then
        log_error "未找到upstream远程仓库"
        exit 1
    fi
}

get_gitcode_token() {
    if [ -n "$GITCODE_TOKEN" ]; then
        echo "$GITCODE_TOKEN"
        return
    fi
    
    log_warn "未检测到GITCODE_TOKEN环境变量"
    read -s -p "请输入GitCode Personal Access Token: " token
    echo
    echo "$token"
}

get_current_branch() {
    git rev-parse --abbrev-ref HEAD
}

push_to_origin() {
    local branch=$1
    log_info "推送分支 $branch 到origin..."
    
    if git push origin "$branch" 2>&1; then
        log_info "推送成功"
        return 0
    else
        log_error "推送失败"
        return 1
    fi
}

get_commit_range() {
    read -p "是否基于最新的单个commit创建PR？(y/n) " use_single_commit
    
    if [ "$use_single_commit" = "y" ]; then
        echo "HEAD~1..HEAD"
        return
    fi
    
    while true; do
        read -p "请输入起始commit ID: " start_commit
        
        if git rev-parse "$start_commit" > /dev/null 2>&1; then
            echo "$start_commit..HEAD"
            return
        else
            log_error "无效的commit ID，请重新输入"
        fi
    done
}

analyze_changes() {
    local commit_range=$1
    
    local files=$(git diff --name-status "$commit_range" | sort)
    local commit_messages=$(git log --format="%s" "$commit_range" | sed 's/^/- /')
    local test_files=$(echo "$files" | grep -E "tests/.*_test\.(cpp|py)|tests/test_.*\.(cpp|py)" || true)
    
    local added_count=$(echo "$files" | grep -c "^A" || echo 0)
    local modified_count=$(echo "$files" | grep -c "^M" || echo 0)
    local deleted_count=$(echo "$files" | grep -c "^D" || echo 0)
    
    echo "FILES=$files"
    echo "COMMIT_MESSAGES=$commit_messages"
    echo "TEST_FILES=$test_files"
    echo "STATS=新增:$added_count 修改:$modified_count 删除:$deleted_count"
}

deep_analyze_changes() {
    local commit_range=$1
    
    log_info "进行深度代码分析..."
    
    local analysis_file="/tmp/pr_analysis_$$.json"
    
    if python3 "$SCRIPT_DIR/code_analyzer.py" "$commit_range" > "$analysis_file" 2>&1; then
        echo "$analysis_file"
        return 0
    else
        log_warn "深度代码分析失败，将使用基础分析"
        echo ""
        return 1
    fi
}

get_pr_type() {
    echo "请选择PR类型标签："
    echo "1) Bug修复"
    echo "2) 新特性"
    echo "3) 代码重构"
    echo "4) 文档更新"
    echo "5) 其他"
    
    while true; do
        read -p "请输入选项(1-5): " choice
        case $choice in
            1) echo "Bug修复"; return ;;
            2) echo "新特性"; return ;;
            3) echo "代码重构"; return ;;
            4) echo "文档更新"; return ;;
            5) 
                read -p "请描述其他类型: " other_type
                echo "其他，请描述：$other_type"
                return
                ;;
            *) log_error "无效选项，请重新输入" ;;
        esac
    done
}

get_pr_title() {
    local commit_messages=$1
    local first_commit=$(echo "$commit_messages" | head -1 | sed 's/^- //')
    
    log_info "默认PR标题: $first_commit"
    read -p "是否使用此标题？(y/n) " use_default
    
    if [ "$use_default" = "y" ]; then
        echo "$first_commit"
        return
    fi
    
    read -p "请输入PR标题: " custom_title
    echo "$custom_title"
}

get_chart_types() {
    echo "请选择需要生成的Mermaid图表类型（可多选，用空格分隔）："
    echo "1) 流程图（flowchart）"
    echo "2) 时序图（sequenceDiagram）"
    echo "3) 类图（classDiagram）"
    echo "4) 架构图（graph）"
    
    read -p "请输入选项（1-4，可多选，留空跳过）: " chart_choices
    
    if [ -z "$chart_choices" ]; then
        echo ""
        return
    fi
    
    local chart_types=""
    for choice in $chart_choices; do
        case $choice in
            1) chart_types="${chart_types}flowchart," ;;
            2) chart_types="${chart_types}sequence," ;;
            3) chart_types="${chart_types}class," ;;
            4) chart_types="${chart_types}architecture," ;;
        esac
    done
    
    echo "${chart_types%,}"
}

generate_detailed_pr_description() {
    local analysis_file=$1
    local chart_types=$2
    
    log_info "生成详细PR描述..."
    
    if [ -z "$analysis_file" ] || [ ! -f "$analysis_file" ]; then
        log_warn "分析文件不存在，生成基础描述"
        echo "## 背景\n本次改动是为了优化代码功能和性能。\n\n"
        echo "## 问题描述\n当前实现存在一些性能和功能上的问题，需要优化。\n\n"
        echo "## 修改方案\n采用优化了案，改进了关键功能的实现。\n\n"
        echo "## 代码流程\n代码执行流程清晰，按步骤处理请求并返回结果。\n\n"
        echo "## 核心逻辑\n核心逻辑设计合理，采用高效的数据结构和算法。\n\n"
        return
    fi
    
    log_info "调用GLM-4.7生成PR描述..."
    
    local desc_file="/tmp/pr_description_$$.md"
    
    if python3 "$SCRIPT_DIR/pr_description_generator.py" \
        --analysis "$analysis_file" \
        --charts "$chart_types" \
        --output "$desc_file" 2>&1; then
        cat "$desc_file"
        rm -f "$desc_file"
    else
        log_warn "LLM生成失败，使用基础描述"
        echo "## 背景\n本次改动是为了优化代码功能和性能。\n\n"
        echo "## 问题描述\n当前实现存在一些性能和功能上的问题，需要优化。\n\n"
        echo "## 修改方案\n采用优化方案，改进了关键功能的实现。\n\n"
        echo "## 代码流程\n代码执行流程清晰，按步骤处理请求并返回结果。\n\n"
        echo "## 核心逻辑\n核心逻辑设计合理，采用高效的数据结构和算法。\n\n"
    fi
}

get_test_info() {
    local test_files=$1
    
    local test_items=""
    local test_results=""
    
    if [ -n "$test_files" ]; then
        log_info "检测到测试文件变更："
        echo -e "$test_files"
        
        test_items="检测到以下测试文件变更：\n"
        test_items+="$test_files"
        
        read -p "是否已运行测试？(y/n) " has_test
        if [ "$has_test" = "y" ]; then
            read -p "请输入测试结果: " test_results
        else
            test_results="<!--请运行测试后填写结果-->"
        fi
    else
        read -p "是否需要添加测试信息？(y/n) " need_test
        if [ "$need_test" = "y" ]; then
            read -p "请输入测试项: " test_items
            read -p "请输入测试结果: " test_results
        else
            test_items="<!--无测试-->"
            test_results="<!--无测试-->"
        fi
    fi
    
    echo "TEST_ITEMS=$test_items"
    echo "TEST_RESULTS=$test_results"
}

get_target_branch() {
    local default="master"
    local target=""
    read -p "请输入目标分支 (默认: $default): " target
    
    if [ -z "$target" ]; then
        echo "$default"
    else
        echo "$target"
    fi
}

fill_pr_template() {
    local pr_type=$1
    local pr_desc=$2
    local test_items=$3
    local test_results=$4
    
    local template
    template=$(cat "$TEMPLATE_FILE")
    
    template=$(echo "$template" | sed "s/- \[ \] $pr_type/- [x] $pr_type/")
    
    template=$(echo "$template" | sed "/<!--简要描述本次改动的背景/c\\
$pr_desc
")
    
    template=$(echo "$template" | sed "/<!--//描述进行了哪些测试/c\\
$test_items
")
    
    template=$(echo "$template" | sed "/<!--描述上述测试项的测试结果/c\\
$test_results
")
    
    echo "$template"
}

call_gitcode_api() {
    local title=$1
    local body=$2
    local head=$3
    local base=$4
    local token=$5
    
    local owner="cann"
    local repo="hixl"
    
    local url="https://gitcode.com/api/v5/repos/$owner/$repo/pulls"
    
    local json_data=$(cat <<EOF
{
    "title": "$title",
    "body": "$body",
    "head": "$head",
    "base": "$base"
}
EOF
)
    
    curl -s -X POST "$url" \
        -H "Authorization: token $token" \
        -H "Content-Type: application/json" \
        -d "$json_data"
}

analyze_error() {
    local response=$1
    
    if echo "$response" | grep -q "401"; then
        echo "Token无效或已过期"
    elif echo "$response" | grep -q "403"; then
        echo "权限不足"
    elif echo "$response" | grep -q "404"; then
        echo "分支或仓库不存在"
    elif echo "$response" | grep -q "429"; then
        echo "API调用频率超限"
    elif echo "$response" | grep -q "timeout"; then
        echo "网络超时"
    else
        echo "未知错误: $response"
    fi
}

try_auto_fix() {
    local error=$1
    
    if echo "$error" | grep -q "Token无效"; then
        log_warn "尝试重新获取Token..."
        return 1
    fi
    
    return 1
}

create_pr_via_api() {
    local pr_title=$1
    local pr_body=$2
    local target_branch=$3
    local token=$4
    local current_branch=$5
    
    local max_retries=3
    local retry_count=0
    local continue_retry=true
    
    while $continue_retry; do
        retry_count=$((retry_count + 1))
        
        log_info "尝试创建PR (第${retry_count}次)..."
        
        local response
        response=$(call_gitcode_api "$pr_title" "$pr_body" "origin:$current_branch" "$target_branch" "$token")
        
        if echo "$response" | grep -q '"html_url"'; then
            local pr_url=$(echo "$response" | grep -o '"html_url":"[^"]*"' | cut -d'"' -f4)
            log_info "PR创建成功！"
            echo "PR链接: $pr_url"
            return 0
        fi
        
        local error_reason
        error_reason=$(analyze_error "$response")
        log_error "API调用失败: $error_reason"
        
        if try_auto_fix "$error_reason"; then
            continue
        fi
        
        if [ $((retry_count % 3)) -eq 0 ]; then
            read -p "是否继续尝试？(y/n) " continue_choice
            if [ "$continue_choice" != "y" ]; then
                continue_retry=false
            fi
        fi
    done
    
    return 1
}

save_pr_description() {
    local pr_title=$1
    local pr_body=$2
    
    local timestamp=$(date +"%Y%m%d_%H%M%S")
    local output_file="$SCRIPT_DIR/pr_description_${timestamp}.md"
    
    cat > "$output_file" <<EOF
# PR标题：$pr_title

$pr_body

---
此文件由 create_pr.sh 自动生成
时间：$(date +"%Y-%m-%d %H:%M:%S")
EOF
    
    log_info "PR描述已保存到: $output_file"
    echo "文件路径: $output_file"
}

cleanup() {
    rm -f /tmp/pr_analysis_$$.json
    rm -f /tmp/pr_description_$$.md
}

trap cleanup EXIT

main() {
    log_info "开始PR创建流程..."
    
    check_git_status
    check_remotes
    
    local token
    token=$(get_gitcode_token)
    
    local current_branch
    current_branch=$(get_current_branch)
    log_info "当前分支: $current_branch"
    
    local commit_range
    commit_range=$(get_commit_range)
    log_info "Commit范围: $commit_range"
    
    if ! push_to_origin "$current_branch"; then
        log_error "推送失败，无法继续"
        exit 1
    fi
    
    log_info "分析变更内容..."
    local changes
    changes=$(analyze_changes "$commit_range")
    
    local files=$(echo "$changes" | grep "^FILES=" | cut -d'=' -f2-)
    local commit_messages=$(echo "$changes" | grep "^COMMIT_MESSAGES=" | cut -d'=' -f2-)
    local test_files=$(echo "$changes" | grep "^TEST_FILES=" | cut -d'=' -f2-)
    local stats=$(echo "$changes" | grep "^STATS=" | cut -d'=' -f2-)
    
    local analysis_file
    analysis_file=$(deep_analyze_changes "$commit_range")
    
    log_info "收集PR信息..."
    local pr_type
    pr_type=$(get_pr_type)
    
    local pr_title
    pr_title=$(get_pr_title "$commit_messages")
    
    local chart_types
    chart_types=$(get_chart_types)
    
    local pr_desc
    pr_desc=$(generate_detailed_pr_description "$analysis_file" "$chart_types")
    
    local test_info
    test_info=$(get_test_info "$test_files")
    local test_items=$(echo "$test_info" | grep "^TEST_ITEMS=" | cut -d'=' -f2-)
    local test_results=$(echo "$test_info" | grep "^TEST_RESULTS=" | cut -d'=' -f2-)
    
    local target_branch
    target_branch=$(get_target_branch)
    
    log_info "填充PR模板..."
    local pr_body
    pr_body=$(fill_pr_template "$pr_type" "$pr_desc" "$test_items" "$test_results")
    
    log_info "创建PR..."
    if create_pr_via_api "$pr_title" "$pr_body" "$target_branch" "$token" "$current_branch"; then
        log_info "PR创建成功！"
    else
        log_error "PR创建失败"
        save_pr_description "$pr_title" "$pr_body"
    fi
}

main "$@"
