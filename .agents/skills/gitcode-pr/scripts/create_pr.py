#!/usr/bin/env python3
# -*- coding: utf-8 -*-
# ----------------------------------------------------------------------------
# Copyright (c) 2026 Huawei Technologies Co., Ltd.
# This program is free software, you can redistribute it and/or modify it under the terms and conditions of
# CANN Open Software License Agreement Version 2.0 (the "License").
# Please refer to the License for details. You may not use this file except in compliance with the License.
# THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
# INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
# See LICENSE in the root of the software repository for the full text of the License.
# ----------------------------------------------------------------------------
"""
使用 GitCode API 创建 Pull Request

使用方法:
    python create_pr.py --title "PR标题" --head "user:branch" --base "develop" --issue "#32"

或使用完整的 body 参数:
    python create_pr.py --title "PR标题" --head "user:branch" --base "develop" --body "完整描述"
"""

import argparse
import os
import sys
from dataclasses import dataclass
from typing import Optional

import requests


def load_pr_template(template_path=None):
    """加载 PR 模板"""
    if template_path is None:
        script_dir = os.path.dirname(os.path.abspath(__file__))
        template_path = os.path.join(script_dir, '..', 'assets', 'pr_template.md')

    if os.path.exists(template_path):
        with open(template_path, 'r', encoding='utf-8') as f:
            return f.read()
    return None


@dataclass
class PRConfig:
    """Pull Request 配置参数"""
    owner: str
    repo: str
    title: str
    head: str
    base: str
    token: str
    body: Optional[str] = None
    issue: Optional[str] = None
    description: Optional[str] = None


def create_pull_request(config: PRConfig):
    """
    创建 Pull Request

    Args:
        config: PR 配置参数

    Returns:
        PR 信息字典，失败返回 None
    """

    # GitCode API 配置
    base_url = "https://gitcode.com/api/v5"

    # 处理 body 参数
    body = config.body
    if body is None:
        if config.issue and config.description:
            # 使用模板
            template = load_pr_template()
            if template:
                body = template.replace('{{issue}}', config.issue).replace('{{description}}', config.description)
        else:
            body = ""

    # PR 信息
    pr_data = {
        "title": config.title,
        "head": config.head,
        "base": config.base,
        "body": body
    }

    print("正在创建 Pull Request...")
    print(f"目标仓库: {config.owner}/{config.repo}")
    print(f"源分支: {config.head}")
    print(f"目标分支: {config.base}")
    print("-" * 50)

    # 设置请求头
    headers = {
        'Authorization': f'Bearer {config.token}',
        'Content-Type': 'application/json',
        'Accept': 'application/json'
    }

    try:
        url = f"{base_url}/repos/{config.owner}/{config.repo}/pulls"
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

    config = PRConfig(
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

    result = create_pull_request(config)

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
