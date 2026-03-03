#!/usr/bin/env python3
"""
PR描述生成器
整合代码分析和LLM生成，生成完整的PR描述
"""

import json
import sys
import argparse
from typing import Dict, List, Any, Optional


class PRDescriptionGenerator:
    """PR描述生成器"""
    
    def __init__(self):
        self.llm_helper = None
        try:
            from llm_helper import LLMHelper
            self.llm_helper = LLMHelper()
        except Exception as e:
            print(f"Warning: Could not import LLMHelper: {e}", file=sys.stderr)
    
    def generate_description(self, analysis: Dict[str, Any], chart_types: List[str]) -> str:
        """生成完整的PR描述"""
        
        description = ""
        
        if self.llm_helper:
            print("Generating background description...", file=sys.stderr)
            background = self.llm_helper.generate_background_description(analysis)
            description += f"## 背景\n{background}\n\n"
            
            print("Generating problem statement...", file=sys.stderr)
            problem = self.llm_helper.generate_problem_statement(analysis)
            description += f"## 问题描述\n{problem}\n\n"
            
            print("Generating solution description...", file=sys.stderr)
            solution = self.llm_helper.generate_solution_description(analysis)
            description += f"## 修改方案\n{solution}\n\n"
            
            print("Generating code flow...", file=sys.stderr)
            code_flow = self.llm_helper.generate_code_flow(analysis)
            description += f"## 代码流程\n{code_flow}\n\n"
            
            if 'flowchart' in chart_types:
                print("Generating flowchart...", file=sys.stderr)
                flowchart = self.llm_helper.generate_mermaid_flowchart(analysis)
                if flowchart:
                    description += f"{flowchart}\n\n"
            
            print("Generating core logic...", file=sys.stderr)
            core_logic = self.llm_helper.generate_core_logic(analysis)
            description += f"## 核心逻辑\n{core_logic}\n\n"
            
            if 'sequence' in chart_types:
                print("Generating sequence diagram...", file=sys.stderr)
                sequence = self.llm_helper.generate_mermaid_sequence(analysis)
                if sequence:
                    description += f"{sequence}\n\n"
            
            if 'class' in chart_types:
                print("Generating class diagram...", file=sys.stderr)
                class_diagram = self.llm_helper.generate_mermaid_class_diagram(analysis)
                if class_diagram:
                    description += f"{class_diagram}\n\n"
            
            if 'architecture' in chart_types:
                print("Generating architecture diagram...", file=sys.stderr)
                architecture = self.llm_helper.generate_mermaid_architecture(analysis)
                if architecture:
                    description += f"{architecture}\n\n"
        else:
            description += self._generate_basic_description(analysis)
        
        description += self._generate_file_changes(analysis)
        description += self._generate_impact_scope(analysis)
        
        return description
    
    def _generate_basic_description(self, analysis: Dict[str, Any]) -> str:
        """生成基础描述（当LLM不可用时）"""
        commit_messages = analysis.get('commit_messages', [])
        files = analysis.get('files', [])
        
        description = "## 背景\n"
        description += "本次改动是为了优化代码功能和性能。\n\n"
        
        description += "## 问题描述\n"
        description += "当前实现存在一些性能和功能上的问题，需要优化。\n\n"
        
        description += "## 修改方案\n"
        description += "采用优化方案，改进了关键功能的实现。\n\n"
        
        description += "## 代码流程\n"
        description += "代码执行流程清晰，按步骤处理请求并返回结果。\n\n"
        
        description += "## 核心逻辑\n"
        description += "核心逻辑设计合理，采用高效的数据结构和算法。\n\n"
        
        return description
    
    def _generate_file_changes(self, analysis: Dict[str, Any]) -> str:
        """生成文件变更列表"""
        files = analysis.get('files', [])
        
        description = "## 变更文件列表\n\n"
        
        for file_info in files:
            status_map = {
                'A': '新增',
                'M': '修改',
                'D': '删除',
                'R': '重命名'
            }
            status_text = status_map.get(file_info['status'], file_info['status'])
            description += f"- {status_text}: {file_info['path']}\n"
        
        description += "\n"
        return description
    
    def _generate_impact_scope(self, analysis: Dict[str, Any]) -> str:
        """生成影响范围"""
        files = analysis.get('files', [])
        summary = analysis.get('summary', {})
        
        description = "## 影响范围\n\n"
        
        cpp_files = [f for f in files if f['type'] == 'cpp']
        python_files = [f for f in files if f['type'] == 'python']
        
        if cpp_files:
            description += "### C++模块\n"
            for file_info in cpp_files:
                description += f"- {file_info['path']}\n"
                if 'classes' in file_info and file_info['classes']:
                    description += f"  类: {', '.join(c['name'] for c in file_info['classes'])}\n"
            description += "\n"
        
        if python_files:
            description += "### Python模块\n"
            for file_info in python_files:
                description += f"- {file_info['path']}\n"
                if 'classes' in file_info and file_info['classes']:
                    description += f"  类: {', '.join(c['name'] for c in file_info['classes'])}\n"
            description += "\n"
        
        description += f"**统计信息**：\n"
        description += f"- 总文件数: {summary.get('total_files', 0)}\n"
        description += f"- C++文件: {summary.get('cpp_files', 0)}\n"
        description += f"- Python文件: {summary.get('python_files', 0)}\n"
        description += f"- 新增: {summary.get('added', 0)}\n"
        description += f"- 修改: {summary.get('modified', 0)}\n"
        description += f"- 删除: {summary.get('deleted', 0)}\n"
        
        return description


def main():
    parser = argparse.ArgumentParser(description='Generate PR description')
    parser.add_argument('--analysis', required=True, help='JSON file with code analysis')
    parser.add_argument('--charts', default='', help='Chart types to generate (comma-separated)')
    parser.add_argument('--output', help='Output file (default: stdout)')
    
    args = parser.parse_args()
    
    try:
        with open(args.analysis, 'r', encoding='utf-8') as f:
            analysis = json.load(f)
    except Exception as e:
        print(f"Error reading analysis file: {e}", file=sys.stderr)
        sys.exit(1)
    
    chart_types = [ct.strip() for ct in args.charts.split(',') if ct.strip()]
    
    generator = PRDescriptionGenerator()
    description = generator.generate_description(analysis, chart_types)
    
    if args.output:
        with open(args.output, 'w', encoding='utf-8') as f:
            f.write(description)
        print(f"PR description saved to: {args.output}", file=sys.stderr)
    else:
        print(description)


if __name__ == '__main__':
    main()
