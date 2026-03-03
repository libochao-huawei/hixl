#!/usr/bin/env python3
"""
LLM API集成模块
使用GLM-4.7大模型生成PR描述和Mermaid图表
"""

import json
import sys
import time
from typing import Dict, List, Any, Optional


class LLMHelper:
    """LLM API帮助类"""
    
    def __init__(self):
        self.config = {
            'model': 'glm-4.7',
            'api_endpoint': 'https://open.bigmodel.cn/api/paas/v4/chat/completions',
            'temperature': 0.7,
            'max_tokens': 2000,
            'timeout': 30,
            'retry_times': 3,
            'retry_delay': 2
        }
    
    def call_llm(self, prompt: str) -> Optional[str]:
        """调用LLM API"""
        import subprocess
        
        for i in range(self.config['retry_times']):
            try:
                payload = {
                    'model': self.config['model'],
                    'messages': [
                        {
                            'role': 'user',
                            'content': prompt
                        }
                    ],
                    'temperature': self.config['temperature'],
                    'max_tokens': self.config['max_tokens']
                }
                
                result = subprocess.run(
                    ['curl', '-s', '-X', 'POST', self.config['api_endpoint'],
                     '-H', 'Content-Type: application/json',
                     '-d', json.dumps(payload)],
                    capture_output=True,
                    text=True,
                    timeout=self.config['timeout']
                )
                
                if result.returncode == 0:
                    response = json.loads(result.stdout)
                    if 'choices' in response and len(response['choices']) > 0:
                        return response['choices'][0]['message']['content']
                
                print(f"LLM API call failed attempt: {i + 1}", file=sys.stderr)
                if i < self.config['retry_times'] - 1:
                    time.sleep(self.config['retry_delay'])
            
            except Exception as e:
                print(f"Error calling LLM API: {e}", file=sys.stderr)
                if i < self.config['retry_times'] - 1:
                    time.sleep(self.config['retry_delay'])
        
        return None
    
    def generate_background_description(self, analysis: Dict[str, Any]) -> str:
        """生成背景描述"""
        commit_messages = analysis.get('commit_messages', [])
        files = analysis.get('files', [])
        
        prompt = f"""你是一个资深的技术文档撰写者。基于以下代码变更信息，生成PR的"背景"部分描述。

代码变更信息：
提交信息：
{chr(10).join(f"- {msg}" for msg in commit_messages[:5])}

变更文件：
{chr(10).join(f"- {f['status']} {f['path']}" for f in files[:10])}

要求：
1. 说明为什么需要这次改动
2. 描述当前的问题或需求
3. 说明改动的业务价值
4. 使用简洁清晰的技术语言
5. 长度控制在200-300字
6. 直接输出描述内容，不要包含其他说明文字

请生成背景描述："""
        
        result = self.call_llm(prompt)
        return result if result else "本次改动是为了优化代码功能和性能。"
    
    def generate_problem_statement(self, analysis: Dict[str, Any]) -> str:
        """生成问题描述"""
        prompt = f"""基于以下代码变更信息，生成PR的"问题描述"部分。

代码变更信息：
{json.dumps(analysis, ensure_ascii=False, indent=2)[:1000]}

要求：
1. 描述当前存在的问题
2. 说明需要解决的痛点
3. 使用简洁清晰的技术语言
4. 长度控制在150-200字
5. 直接输出描述内容，不要包含其他说明文字

请生成问题描述："""
        
        result = self.call_llm(prompt)
        return result if result else "当前实现存在一些性能和功能上的问题，需要优化。"
    
    def generate_solution_description(self, analysis: Dict[str, Any]) -> str:
        """生成修改方案"""
        prompt = f"""基于以下代码变更信息，生成PR的"修改方案"部分。

代码变更信息：
{json.dumps(analysis, ensure_ascii=False, indent=2)[:1500]}

要求：
1. 说明采用的技术方案
2. 列出关键改动点
3. 说明实现思路
4. 使用简洁清晰的技术语言
5. 长度控制在300-400字
6. 直接输出描述内容，不要包含其他说明文字

请生成修改方案："""
        
        result = self.call_llm(prompt)
        return result if result else "采用优化方案，改进了关键功能的实现。"
    
    def generate_code_flow(self, analysis: Dict[str, Any]) -> str:
        """生成代码流程说明"""
        prompt = f"""基于以下代码变更信息，生成PR的"代码流程"部分说明。

代码变更信息：
{json.dumps(analysis, ensure_ascii=False, indent=2)[:1000]}

要求：
1. 说明主要执行流程
2. 列出关键步骤
3. 说明数据流向
4. 使用简洁清晰的技术语言
5. 长度控制在200-300字
6. 直接输出描述内容，不要包含其他说明文字

请生成代码流程说明："""
        
        result = self.call_llm(prompt)
        return result if result else "代码执行流程清晰，按步骤处理请求并返回结果。"
    
    def generate_core_logic(self, analysis: Dict[str, Any]) -> str:
        """生成核心逻辑说明"""
        prompt = f"""基于以下代码变更信息，生成PR的"核心逻辑"部分说明。

代码变更信息：
{json.dumps(analysis, ensure_ascii=False, indent=2)[:1000]}

要求：
1. 说明关键算法
2. 描述核心数据结构
3. 说明重要设计决策
4. 使用简洁清晰的技术语言
5. 长度控制在200-300字
6. 直接输出描述内容，不要包含其他说明文字

请生成核心逻辑说明："""
        
        result = self.call_llm(prompt)
        return result if result else "核心逻辑设计合理，采用高效的数据结构和算法。"
    
    def generate_mermaid_flowchart(self, analysis: Dict[str, Any]) -> Optional[str]:
        """生成Mermaid流程图"""
        prompt = f"""基于以下代码变更信息，生成Mermaid流程图代码。

代码变更信息：
{json.dumps(analysis, ensure_ascii=False, indent=2)[:1000]}

要求：
1. 使用flowchart语法
2. 展示主要代码执行流程
3. 标注关键步骤和决策点
4. 使用清晰的节点命名
5. 保持图表简洁易读
6. 只输出mermaid代码块，不要包含其他说明文字

示例格式：
```mermaid
flowchart TD
    A[开始] --> B[步骤1]
    B --> C{判断}
    C -->|yes| D[处理A]
    C -->|no| E[处理B]
    D --> F[结束]
    E --> F
```

请生成流程图："""
        
        result = self.call_llm(prompt)
        return result
    
    def generate_mermaid_sequence(self, analysis: Dict[str, Any]) -> Optional[str]:
        """生成Mermaid时序图"""
        prompt = f"""基于以下代码变更信息，生成Mermaid时序图代码。

代码变更信息：
{json.dumps(analysis, ensure_ascii=False, indent=2)[:1000]}

要求：
1. 使用sequenceDiagram语法
2. 展示组件间的交互时序
3. 标注关键消息传递
4. 使用清晰的参与者命名
5. 保持图表逻辑清晰
6. 只输出mermaid代码块，不要包含其他说明文字

示例格式：
```mermaid
sequenceDiagram
    participant A as 组件A
    participant B as 组件B
    A->>B: 调用方法
    B-->>A: 返回结果
```

请生成时序图："""
        
        result = self.call_llm(prompt)
        return result
    
    def generate_mermaid_class_diagram(self, analysis: Dict[str, Any]) -> Optional[str]:
        """生成Mermaid类图"""
        prompt = f"""基于以下代码变更信息，生成Mermaid类图代码。

代码变更信息：
{json.dumps(analysis, ensure_ascii=False, indent=2)[:1000]}

要求：
1. 使用classDiagram语法
2. 展示类和依赖关系
3. 标注关键方法和属性
4. 使用清晰的类命名
5. 保持图表简洁易读
6. 只输出mermaid代码块，不要包含其他说明文字

示例格式：
```mermaid
classDiagram
    class ClassA
    class ClassB
    ClassA --> ClassB
```

请生成类图："""
        
        result = self.call_llm(prompt)
        return result
    

    
    def generate_mermaid_architecture(self, analysis: Dict[str, Any]) -> Optional[str]:
        """生成Mermaid架构图"""
        prompt = f"""基于以下代码变更信息，生成Mermaid架构图代码。

代码变更信息：
{json.dumps(analysis, ensure_ascii=False, indent=2)[:1000]}

要求：
1. 使用graph语法
2. 展示模块和组件架构
3. 标注模块间的关系
4. 使用清晰的模块命名
5. 保持图表简洁易读
6. 只输出mermaid代码块，不要包含其他说明文字

示例格式：
```mermaid
graph TD
    subgraph 模块A
        A1[组件1]
        A2[组件2]
    end
    subgraph 模块B
        B1[组件3]
    end
    A1 --> B1
```

请生成架构图："""
        
        result = self.call_llm(prompt)
        return result


def main():
    """测试LLM功能"""
    if len(sys.argv) < 2:
        print("Usage: llm_helper.py <analysis.json>", file=sys.stderr)
        sys.exit(1)
    
    with open(sys.argv[1], 'r', encoding='utf-8') as f:
        analysis = json.load(f)
    
    helper = LLMHelper()
    
    print("生成背景描述...")
    background = helper.generate_background_description(analysis)
    print(f"背景: {background}\n")
    
    print("生成问题描述...")
    problem = helper.generate_problem_statement(analysis)
    print(f"问题: {problem}\n")
    
    print("生成修改方案...")
    solution = helper.generate_solution_description(analysis)
    print(f"方案: {solution}\n")


if __name__ == '__main__':
    main()
