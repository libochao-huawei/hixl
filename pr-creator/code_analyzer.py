#!/usr/bin/env python3
"""
HIXL代码深度分析工具
用于深度分析代码变更，提取类、方法、调用关系等信息
"""

import ast
import re
import subprocess
import json
import sys
from pathlib import Path
from typing import Dict, List, Any, Optional


class CppAnalyzer:
    """C++代码分析器"""
    
    def __init__(self, filepath: str):
        self.filepath = filepath
        try:
            with open(filepath, 'r', encoding='utf-8') as f:
                self.content = f.read()
        except Exception as e:
            print(f"Error reading file {filepath}: {e}", file=sys.stderr)
            self.content = ""
    
    def extract_classes(self) -> List[Dict[str, Any]]:
        """提取类定义"""
        pattern = r'class\s+(\w+)(?:\s*:\s*(?:public|private|protected)\s+(\w+))?'
        matches = re.finditer(pattern, self.content)
        classes = []
        for match in matches:
            class_name = match.group(1)
            base_class = match.group(2) if match.group(2) else None
            classes.append({
                'name': class_name,
                'base': base_class
            })
        return classes
    
    def extract_methods(self) -> List[Dict[str, Any]]:
        """提取方法定义"""
        pattern = r'(\w+(?:\s*::\s*\w+)*)\s+(\w+)\s*\(([^)]*)\)\s*(const\s*)?(?:override\s*)?(?:=\s*\w+)?\s*\{'
        matches = re.finditer(pattern, self.content)
        methods = []
        for match in matches:
            return_type = match.group(1)
            method_name = match.group(2)
            params = match.group(3)
            is_const = bool(match.group(4))
            methods.append({
                'return_type': return_type,
                'name': method_name,
                'params': params,
                'is_const': is_const
            })
        return methods
    
    def extract_function_calls(self) -> List[str]:
        """提取函数调用"""
        pattern = r'(\w+)\s*\('
        matches = re.findall(pattern, self.content)
        return list(set(matches))
    
    def analyze_diff(self, old_content: str) -> Dict[str, Any]:
        """分析代码差异"""
        if not old_content:
            return {'type': 'new', 'added_lines': len(self.content.split('\n'))}
        
        old_lines = old_content.split('\n')
        new_lines = self.content.split('\n')
        
        added = []
        removed = []
        modified = []
        
        for i, (old_line, new_line) in enumerate(zip(old_lines, new_lines)):
            if old_line != new_line:
                modified.append({
                    'line': i + 1,
                    'old': old_line,
                    'new': new_line
                })
        
        if len(new_lines) > len(old_lines):
            added.extend(range(len(old_lines) + 1, len(new_lines) + 1))
        elif len(new_lines) < len(old_lines):
            removed.extend(range(len(new_lines) + 1, len(old_lines) + 1))
        
        return {
            'type': 'modified',
            'added_lines': added,
            'removed_lines': removed,
            'modified_lines': modified
        }


class PythonAnalyzer:
    """Python代码分析器"""
    
    def __init__(self, filepath: str):
        self.filepath = filepath
        try:
            with open(filepath, 'r', encoding='utf-8') as f:
                self.content = f.read()
            self.tree = ast.parse(self.content)
        except Exception as e:
            print(f"Error parsing file {filepath}: {e}", file=sys.stderr)
            self.content = ""
            self.tree = None
    
    def extract_classes(self) -> List[Dict[str, Any]]:
        """提取类定义"""
        if not self.tree:
            return []
        
        classes = []
        for node in ast.walk(self.tree):
            if isinstance(node, ast.ClassDef):
                methods = []
                for item in node.body:
                    if isinstance(item, ast.FunctionDef):
                        args = [arg.arg for arg in item.args.args]
                        returns = ast.unparse(item.returns) if item.returns else None
                        methods.append({
                            'name': item.name,
                            'args': args,
                            'returns': returns
                        })
                
                bases = []
                for base in node.bases:
                    if isinstance(base, ast.Name):
                        bases.append(base.id)
                    else:
                        bases.append(ast.unparse(base))
                
                classes.append({
                    'name': node.name,
                    'bases': bases,
                    'methods': methods
                })
        return classes
    
    def extract_functions(self) -> List[Dict[str, Any]]:
        """提取函数定义"""
        if not self.tree:
            return []
        
        functions = []
        for node in ast.walk(self.tree):
            if isinstance(node, ast.FunctionDef) and not any(
                isinstance(parent, ast.ClassDef) for parent in ast.walk(self.tree)
                if hasattr(parent, 'body') and node in parent.body
            ):
                args = [arg.arg for arg in node.args.args]
                returns = ast.unparse(node.returns) if node.returns else None
                functions.append({
                    'name': node.name,
                    'args': args,
                    'returns': returns
                })
        return functions
    
    def extract_imports(self) -> List[str]:
        """提取导入依赖"""
        if not self.tree:
            return []
        
        imports = []
        for node in ast.walk(self.tree):
            if isinstance(node, ast.Import):
                for alias in node.names:
                    imports.append(alias.name)
            elif isinstance(node, ast.ImportFrom):
                module = node.module if node.module else ''
                for alias in node.names:
                    imports.append(f"{module}.{alias.name}")
        return imports
    
    def extract_function_calls(self) -> List[str]:
        """提取函数调用"""
        if not self.tree:
            return []
        
        calls = []
        for node in ast.walk(self.tree):
            if isinstance(node, ast.Call):
                if isinstance(node.func, ast.Name):
                    calls.append(node.func.id)
                elif isinstance(node.func, ast.Attribute):
                    calls.append(f"{ast.unparse(node.func.value)}.{node.func.attr}")
        return list(set(calls))


class GitChangeAnalyzer:
    """Git变更分析器"""
    
    @staticmethod
    def get_changed_files(commit_range: str) -> List[Dict[str, Any]]:
        """获取变更文件列表"""
        try:
            result = subprocess.run(
                ['git', 'diff', '--name-status', commit_range],
                capture_output=True, text=True, check=True
            )
            
            files = []
            for line in result.stdout.strip().split('\n'):
                if line:
                    parts = line.split('\t', 1)
                    if len(parts) == 2:
                        status, filepath = parts
                        files.append({
                            'status': status,
                            'path': filepath,
                            'type': 'cpp' if filepath.endswith(('.cpp', '.cc', '.h', '.hpp')) else 
                                   'python' if filepath.endswith('.py') else 'other'
                        })
            return files
        except subprocess.CalledProcessError as e:
            print(f"Error getting changed files: {e}", file=sys.stderr)
            return []
    
    @staticmethod
    def get_commit_messages(commit_range: str) -> List[str]:
        """获取提交信息"""
        try:
            result = subprocess.run(
                ['git', 'log', '--format=%s', commit_range],
                capture_output=True, text=True, check=True
            )
            return [msg for msg in result.stdout.strip().split('\n') if msg]
        except subprocess.CalledProcessError as e:
            print(f"Error getting commit messages: {e}", file=sys.stderr)
            return []
    
    @staticmethod
    def get_file_diff(commit_range: str, filepath: str) -> str:
        """获取文件差异"""
        try:
            result = subprocess.run(
                ['git', 'diff', commit_range, '--', filepath],
                capture_output=True, text=True, check=True
            )
            return result.stdout
        except subprocess.CalledProcessError as e:
            print(f"Error getting file diff: {e}", file=sys.stderr)
            return ""
    
    @staticmethod
    def get_old_file_content(commit_range: str, filepath: str) -> str:
        """获取旧版本文件内容"""
        try:
            start_commit = commit_range.split('..')[0]
            result = subprocess.run(
                ['git', 'show', f"{start_commit}:{filepath}"],
                capture_output=True, text=True
            )
            return result.stdout if result.returncode == 0 else ""
        except Exception as e:
            print(f"Error getting old file content: {e}", file=sys.stderr)
            return ""


def analyze_code_changes(commit_range: str) -> Dict[str, Any]:
    """分析代码变更"""
    print(f"Analyzing code changes for range: {commit_range}", file=sys.stderr)
    
    analyzer = GitChangeAnalyzer()
    
    changed_files = analyzer.get_changed_files(commit_range)
    commit_messages = analyzer.get_commit_messages(commit_range)
    
    analysis = {
        'commit_range': commit_range,
        'commit_messages': commit_messages,
        'files': [],
        'summary': {
            'total_files': len(changed_files),
            'cpp_files': 0,
            'python_files': 0,
            'other_files': 0,
            'added': 0,
            'modified': 0,
            'deleted': 0
        }
    }
    
    for file_info in changed_files:
        status = file_info['status']
        filepath = file_info['path']
        file_type = file_info['type']
        
        file_analysis = {
            'path': filepath,
            'status': status,
            'type': file_type
        }
        
        if status == 'A':
            analysis['summary']['added'] += 1
        elif status == 'M':
            analysis['summary']['modified'] += 1
        elif status == 'D':
            analysis['summary']['deleted'] += 1
        
        if file_type == 'cpp':
            analysis['summary']['cpp_files'] += 1
            if status != 'D':
                cpp_analyzer = CppAnalyzer(filepath)
                file_analysis['classes'] = cpp_analyzer.extract_classes()
                file_analysis['methods'] = cpp_analyzer.extract_methods()
                file_analysis['calls'] = cpp_analyzer.extract_function_calls()
                
                if status == 'M':
                    old_content = analyzer.get_old_file_content(commit_range, filepath)
                    file_analysis['diff'] = cpp_analyzer.analyze_diff(old_content)
        
        elif file_type == 'python':
            analysis['summary']['python_files'] += 1
            if status != 'D':
                py_analyzer = PythonAnalyzer(filepath)
                file_analysis['classes'] = py_analyzer.extract_classes()
                file_analysis['functions'] = py_analyzer.extract_functions()
                file_analysis['imports'] = py_analyzer.extract_imports()
                file_analysis['calls'] = py_analyzer.extract_function_calls()
                
                if status == 'M':
                    old_content = analyzer.get_old_file_content(commit_range, filepath)
                    file_analysis['diff'] = py_analyzer.analyze_diff(old_content)
        
        else:
            analysis['summary']['other_files'] += 1
        
        analysis['files'].append(file_analysis)
    
    return analysis


def main():
    if len(sys.argv) < 2:
        print("Usage: code_analyzer.py <commit_range>", file=sys.stderr)
        print("Example: code_analyzer.py HEAD~1..HEAD", file=sys.stderr)
        sys.exit(1)
    
    commit_range = sys.argv[1]
    
    try:
        analysis = analyze_code_changes(commit_range)
        print(json.dumps(analysis, indent=2, ensure_ascii=False))
    except Exception as e:
        print(f"Error: {e}", file=sys.stderr)
        sys.exit(1)


if __name__ == '__main__':
    main()
