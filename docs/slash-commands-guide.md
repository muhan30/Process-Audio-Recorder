# Claude Code Slash Commands 速查手册

> 列出所有可用的 `/` 命令及其作用。分三类来源：Claude Code 内置、Superpowers 套件、其他专业套件。
> 生成日期：2026-07-18

---

## 一、Claude Code 内置命令（所有用户都有）

### 会话管理

| 命令 | 作用 |
|------|------|
| `/help` | 显示帮助文档 |
| `/clear` | 清空当前对话，重新开始 |
| `/compact` | 压缩上下文——对话太长时把前面的内容压缩成摘要，防止超出 token 限制 |
| `/context` | 显示当前上下文信息（文件、记忆、项目状态等） |
| `/resume` | 恢复历史对话会话 |
| `/export` | 导出当前对话记录 |

### 配置与个性化

| 命令 | 作用 |
|------|------|
| `/init` | 初始化项目的 CLAUDE.md（项目级开发规范） |
| `/config` | 修改 Claude Code 全局配置 |
| `/theme` | 切换终端配色主题 |
| `/statusline` | 配置终端状态栏显示内容 |
| `/output-style` | 修改 AI 输出风格 |
| `/permissions` | 管理工具权限（bash 执行、文件写入等是否需确认） |
| `/terminal-setup` | 终端集成设置 |

### 项目与工作区

| 命令 | 作用 |
|------|------|
| `/add-dir` | 添加额外工作目录 |
| `/workspace-diff` | 查看工作区文件变更 |
| `/memory` | 查看/管理项目级 memory 文件 |
| `/todos` | 查看当前待办列表 |

### Agent 与 Skill

| 命令 | 作用 |
|------|------|
| `/agents` | 列出可用的子 agent 类型（Explore、Plan、general-purpose 等） |
| `/skills` | 列出所有已安装的 skill |
| `/mcp` | 管理 MCP 服务器（外部工具集成） |

### 账号与版本

| 命令 | 作用 |
|------|------|
| `/login` | 登录 Claude 账号 |
| `/logout` | 登出 Claude 账号 |
| `/cost` | 查看当前会话 token 用量和费用 |
| `/doctor` | 诊断 Claude Code 环境是否正常 |
| `/upgrade` | 升级 Claude Code 到最新版本 |
| `/release-notes` | 查看 Claude Code 更新日志 |

### GitHub 集成

| 命令 | 作用 |
|------|------|
| `/install-github-app` | 安装 GitHub App 到仓库 |
| `/pr-comments` | 查看/管理 PR 评论 |
| `/review-pr` | 审查一个 Pull Request |

### IDE 集成

| 命令 | 作用 |
|------|------|
| `/ide` | IDE（VSCode/JetBrains）集成相关设置 |

---

## 二、Superpowers 套件（开发流程五阶段）

> 安装路径：`C:\Users\muhan\.claude\skills\superpowers\`
> 这是开发录音 APP 时使用的核心流程。对应 SPEC → PLAN → CODE → REVIEW → VERIFY。

### 核心流程（按顺序）

| 命令 | 对应阶段 | 作用 |
|------|---------|------|
| `/brainstorming` | **SPEC 之前** | 头脑风暴：把模糊需求变成清晰的功能设计。可生成可视化方案对比，帮助用户做选择。 |
| `/writing-plans` | **PLAN** | 写实现计划：列出要改哪些文件、数据流怎么走、测试方案是什么、对已有模块有什么影响。 |
| `/executing-plans` | **CODE** | 按计划逐步实施代码，每步编译确认不崩。 |
| `/requesting-code-review` | **REVIEW** | AI 自查代码，输出审查报告（问题列表、严重度、修复建议、与 PLAN 的一致性）。 |
| `/receiving-code-review` | **REVIEW 后** | 接收审查意见后，逐个处理修复或说明不修的原因。 |
| `/verification-before-completion` | **VERIFY** | 最终验证：编译、功能测试、确认一切正确再宣布完成。 |

### 辅助流程

| 命令 | 作用 |
|------|------|
| `/test-driven-development` | 测试驱动开发：先写测试用例，再写业务代码，确保代码通过测试。 |
| `/systematic-debugging` | 系统性调试：不是乱试，而是按方法论系统性排查。含根因追溯、压力测试、防御性修复三步。 |
| `/subagent-driven-development` | 子 agent 驱动开发：把大型功能拆给多个子 agent 并行实现，各自审查后再合并。 |
| `/dispatching-parallel-agents` | 并行调度 agent：把互相独立的多个任务分配给不同 agent 同时跑，节省时间。 |
| `/using-git-worktrees` | 使用 git worktree：在隔离的工作目录里开发，不影响主分支。 |
| `/finishing-a-development-branch` | 开发分支收尾：合并、清理 worktree、更新文档，一条龙完成。 |
| `/writing-skills` | 教你编写自己的自定义 skill（高级用法，一般用不上）。 |

---

## 三、Web Access（上网查资料）

> 安装路径：`C:\Users\muhan\.claude\skills\web-access\`

| 命令 | 作用 |
|------|------|
| `/web-access` | 通过本地 Chrome 浏览器（CDP 端口 9222）访问网页。**适用于需要登录态才能看的页面**（付费文档、论坛帖子），可以借用 Chrome 里已有的 cookie。 |

> 区别于内置 `WebSearch`（搜公开网页）和 `WebFetch`（匿名抓取），这个能访问需要登录的内容。

---

## 四、Matt Pocock 套件（TypeScript/前端工程）

> 安装路径：`C:\Users\muhan\.claude\skills\mattpocock-skills\`
> Matt Pocock 是 TypeScript 圈知名专家。这套主要用于 TypeScript/React/Node.js 项目。
> ⚠️ 对当前 C++ 项目基本用不上，但做前端/全栈项目时很有价值。

| 命令 | 作用 |
|------|------|
| `/setup-matt-pocock-skills` | 首次配置：关联 GitHub issue tracker、设定项目上下文等 |
| `/diagnose` | 诊断 TypeScript/Node.js 项目的问题（类型错误、运行时异常等） |
| `/grill-with-docs` | "文档审问"：用项目已有的设计文档/ADR 去严格审查代码，看实现是否偏离设计 |
| `/improve-codebase-architecture` | 改进代码架构：接口设计优化、模块分层、语言风格统一 |
| `/prototype` | 快速原型：从零快速搭出可用的 UI + 逻辑，用于验证想法 |

---

## 五、Nature 学术套件（论文写作）

> 安装路径：`C:\Users\muhan\.claude\skills\nature-skills\`
> ⚠️ 完全与录音 APP 项目无关，用于学术写作场景。

| 命令 | 作用 |
|------|------|
| `/nature-academic-search` | 学术文献搜索 |
| `/nature-reader` | 精读论文，提取关键信息 |
| `/nature-writing` | 学术写作辅助（按期刊格式） |
| `/nature-polishing` | 论文语言润色（学术英语） |
| `/nature-citation` | 引用格式管理 |
| `/nature-data` | 科研数据处理与分析 |
| `/nature-figure` | 论文配图/图表制作 |
| `/nature-paper2ppt` | 论文内容转 PPT 汇报 |
| `/nature-response` | 写审稿人回复信 |

---

## 使用方式

- **直接输入命令**：在聊天框输入 `/命令名`（如 `/brainstorming`）
- **自然语言触发**：直接说"帮我审查代码"，AI 会自动调用对应的 `/requesting-code-review` skill
- **当前项目最常用**：Superpowers 那套（五阶段流程）+ `/compact` + `/memory`
