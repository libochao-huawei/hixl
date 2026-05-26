#!/usr/bin/env python3
"""
使用 GitCode API 创建 Pull Request

使用方法:
    python create_pr.py --title "PR标题" --head "user:branch" --base "develop" --issue "#32"

或使用完整的 body 参数:
    python create_pr.py --title "PR标题" --head "user:branch" --base "develop" --body "完整描述"
"""

import requests
import sys
import os
import argparse


def load_pr_template(template_path=None):
    """加载 PR 模板"""
    if template_path is None:
        script_dir = os.path.dirname(os.path.abspath(__file__))
        template_path = os.path.join(script_dir, '..', 'assets', 'pr_template.md')

    if os.path.exists(template_path):
        with open(template_path, 'r', encoding='utf-8') as f:
            return f.read()
    return None


def create_pull_request(
    owner,
    repo,
    title,
    head,
    base,
    token,
    body=None,
    issue=None,
    description=None,
):
    """
    创建 Pull Request

    Args:
        owner: 仓库所有者
        repo: 仓库名称
        title: PR 标题
        head: 源分支 (格式: "username:branch")
        base: 目标分支
        token: GitCode 访问令牌
        body: PR 描述完整内容
        issue: 关联的 issue 编号 (如 "#32")
        description: PR 描述摘要 (用于填充模板)

    Returns:
        PR 信息字典，失败返回 None
    """

    # GitCode API 配置
    base_url = "https://gitcode.com/api/v5"

    # 处理 body 参数
    if body is None:
        if issue and description:
            # 使用模板
            template = load_pr_template()
            if template:
                body = template.replace('{{issue}}', issue).replace('{{description}}', description)
        else:
            body = ""

    # PR 信息
    pr_data = {
        "title": title,
        "head": head,
        "base": base,
        "body": body
    }

    print("正在创建 Pull Request...")
    print(f"目标仓库: {owner}/{repo}")
    print(f"源分支: {head}")
    print(f"目标分支: {base}")
    print("-" * 50)

    # 设置请求头
    headers = {
        'Authorization': f'Bearer {token}',
        'Content-Type': 'application/json',
        'Accept': 'application/json'
    }

    try:
        url = f"{base_url}/repos/{owner}/{repo}/pulls"
        response = requests.post(url, headers=headers, json=pr_data, timeout=30)

        # GitCode API 返回 200 也表示成功
        if response.status_code in [200, 201]:
            result = response.json()
            print("✅ Pull Request 创建成功！")
            print(f"PR 编号: #{result.get('number', '未知')}")
            print(f"PR 链接: {result.get('web_url', result.get('html_url', '获取失败'))}")
            print(f"状态: {result.get('state', '未知')}")
            return result
        else:
            print(f"❌ 创建失败，状态码: {response.status_code}")
            print(f"响应内容: {response.text}")
            return None

    except requests.exceptions.RequestException as e:
        print(f"❌ 请求异常: {e}")
        return None


def main():
    """主函数"""
    parser = argparse.ArgumentParser(description='使用 GitCode API 创建 Pull Request')
    parser.add_argument('--owner', default='cann', help='仓库所有者 (默认: cann)')
    parser.add_argument('--repo', default='metadef', help='仓库名称 (默认: metadef)')
    parser.add_argument('--title', required=True, help='PR 标题')
    parser.add_argument('--head', required=True, help='源分支 (格式: username:branch)')
    parser.add_argument('--base', default='develop', help='目标分支 (默认: develop)')
    parser.add_argument('--body', help='PR 描述完整内容')
    parser.add_argument('--issue', help='关联的 issue 编号 (如: #32)')
    parser.add_argument('--description', help='PR 描述摘要')
    parser.add_argument('--token', help='GitCode 访问令牌')

    args = parser.parse_args()

    result = create_pull_request(
        owner=args.owner,
        repo=args.repo,
        title=args.title,
        head=args.head,
        base=args.base,
        body=args.body,
        issue=args.issue,
        description=args.description or "",
        token=args.token
    )

    if result:
        print("\n" + "=" * 50)
        print("下一步建议：")
        print("1. 检查 PR 状态和自动化检查结果")
        print("2. 准备好回复审查者可能提出的问题")
        print("3. 关注 CI/CD 运行结果")
        sys.exit(0)
    else:
        print("\n请检查错误信息并根据提示进行调整")
        sys.exit(1)


if __name__ == "__main__":
    main()
