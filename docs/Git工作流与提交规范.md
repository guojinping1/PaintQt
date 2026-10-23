# Git 工作流与提交规范

按《绘图功能开发与代码架构调整沟通》中「代码全部上传至 GitHub 对应分支」「每次功能变更需提交规范的 commit 记录」制定的约定。

---

## 一、分支模型

| 分支 | 用途 | 规则 |
| --- | --- | --- |
| `main` | 随时可交付的主线 | 只接受合并，不直接在上面写代码 |
| `feature/<功能名>` | 新功能开发 | 从 `main` 拉出，完成后合并回去 |
| `fix/<问题名>` | 修 bug | 同上 |

**合并方式：`git merge --no-ff`**

用 `--no-ff` 强制生成一个合并提交，这样提交图谱里能看出"这个功能是一整条分支做的"，而不是被压成一条直线看不出工作过程。

```
*   8ff8348 合并分支 feature/gameobject-base：引入 GameObject 基类
|\
| * 1b3cf26 重构：引入 GameObject 基类，绘图功能类改为继承它
|/
* f9eaba6 初始提交：工程结构 + 三个绘图静态库 + Qt 画图板
```

---

## 二、提交信息格式

```
<类型>：<一句话说清做了什么>

<正文：为什么这么做、改了哪些地方、有什么取舍>

<页脚：怎么验证的>
```

**类型**（中文，冒号用全角）：

| 类型 | 用在哪 |
| --- | --- |
| `新增` | 加功能、加文件 |
| `重构` | 改结构但不改行为 |
| `修复` | 改掉一个 bug |
| `文档` | 只改 .md / 注释 |
| `构建` | CMake、编译选项、依赖 |
| `样式` | 只改界面外观 / 格式 |

**标题**：不超过 50 字，说的是"做了什么"，不是"改了哪个文件"。

- ✅ `新增：DXF 矢量文件导入解析`
- ❌ `update main.cpp`
- ❌ `改了一下`

**正文**：先说**为什么**，再说改了哪些。有设计取舍的一定写出来——审查代码的人最想知道的就是"为什么不用另一种做法"。

**页脚**：写清楚怎么验证的（编译通过？跑了什么操作？）。

---

## 三、一条提交只做一件事

反面例子：

```
新增 DXF 导入 + 修了几个 bug + 改了界面配色
```

这样出问题没法单独回滚，审查时也没法只看一个改动。

正确做法是拆成三条：

```
新增：DXF 矢量文件导入解析
修复：SVG path 的 A 圆弧命令未计入统计
样式：左侧功能区改用浅灰背景
```

---

## 四、不提交的东西

`.gitignore` 已经排除：

```gitignore
build/                  CMake / 编译产物
CMakeUserPresets.json   本机私有的预设
.vscode/                本机 IDE 配置（里面是绝对路径）
*.user                  VS 的本机用户配置
```

判断标准：**换一台机器能重新生成的，都不要提交。**

---

## 五、本仓库已有的提交

```
8ff8348  合并分支 feature/gameobject-base：引入 GameObject 基类
1b3cf26  重构：引入 GameObject 基类，绘图功能类改为继承它
f9eaba6  初始提交：工程结构 + 三个绘图静态库 + Qt 画图板
```

---

## 六、常用命令

```powershell
# 看当前状态
git status
git log --graph --oneline --all

# 开分支做功能
git checkout -b feature/dxf-import
git add -A
git commit -F message.txt        # 长提交信息建议写成文件再 -F，避免命令行转义问题

# 做完合回主线
git checkout main
git merge --no-ff feature/dxf-import

# 撤销还没提交的改动（危险，会丢掉工作）
git restore .

# 回到某个提交的内容
git show f9eaba6:draw/include/Draw.h
```

---

## 七、还没有做的事

**代码还没推到 GitHub**，因为本机的 Git 凭据还没配好。等配好后执行：

```powershell
git remote add origin https://github.com/guojinping1/<仓库名>.git
git push -u origin main
git push origin feature/gameobject-base     # 分支也推上去
```

按文档要求「代码全部上传至 GitHub 对应分支」，**分支本身也要推**，不能只推 main。
