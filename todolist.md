# 待办功能清单

> 记录今天想做但暂时搁置、留待以后实现的功能点。每个条目使用稳定前缀，方便对话中按编号引用。

---

## 记录格式

每个条目包含以下信息：

```markdown
- [ ] TODO-001 【描述】一句话概括功能点
  - 上下文：为什么想做 / 阻塞原因 / 依赖项
  - 日期：YYYY-MM-DD
```

已完成条目移动到「已完成」，并补充完成日期和完成依据。

---

## 手动收集

> 在这里随手写不规范需求。之后调用 `$todo-normalize-inbox`，将本区内容整理为正式待办项。

动画出现重复加载的问题，需要修复。

---

## 待办

- [ ] TODO-006 【引擎/调试】支持 Agent 截图场景来调试和验证功能点
  - 上下文：从手动收集区整理；调试时需手动截图上传到外部 LLM 分析，流程繁琐。在 tinyEngine 中内置截图/帧捕获功能（如 RenderDoc 触发、离屏渲染到文件、或 Vulkan 帧缓冲导出），使 agent 可直接获取渲染画面进行分析和验证
  - 日期：2026-06-18
  - 来源：手动收集

- [ ] TODO-007 【动画/资产】将动画模型资产和动画序列资产分离为不同文件
  - 上下文：从手动收集区整理；当前动画数据随 glTF 一起加载，动画片段无独立文件格式存储和复用。参考 Phase A5 计划，需要实现 .anim.json 或 .anim 格式的独立动画资产文件，支持序列化/反序列化 AnimationClip
  - 日期：2026-06-18
  - 来源：手动收集

---

## 已完成

- [x] TODO-001 【Content Browser】新增文件夹功能，import 的 .ast 放在当前浏览的文件夹下
  - 上下文：目前所有 .ast 都平铺在 materials/ 下，无组织
  - 日期：2026-06-11
  - 完成日期：2026-06-17
  - 完成依据：Content Browser 支持创建并切换文件夹；导入入口 .ast 与 glTF 子材质 .ast 均写入当前文件夹；x64-debug 构建通过

- [x] TODO-002 【Content Browser】新增 filter，根据 .ast 的 type 字段过滤显示
  - 上下文：Mesh 和 Material 类型混在一起，需要分类查看
  - 日期：2026-06-11
  - 完成日期：2026-06-17
  - 完成依据：ModelRegistry 读取 .ast 的 type 字段，Content Browser 提供 All/Mesh/Box/Material 筛选；x64-debug 构建通过

- [x] TODO-003 【.ast 格式】新增 "Material" 类型（type="Material"），不含 model 字段
  - 上下文：纯材质资产（不关联模型）需要独立表示；Material 类型的 ast 不能拖入场景
  - 日期：2026-06-11
  - 完成日期：2026-06-17
  - 完成依据：MaterialAssetLoader 解析 type="Material"，MaterialManager 保留运行时 Material 类型并继续使用 Mesh 渲染路径；x64-debug 构建通过

- [x] TODO-004 【Content Browser】Material 类型的 .ast 禁止拖拽放置到场景
  - 上下文：Material ast 无 model 引用，拖拽无意义
  - 依赖：.ast 新增 Material 类型
  - 日期：2026-06-11
  - 完成日期：2026-06-17
  - 完成依据：Content Browser 不为 Material 资产创建拖拽源，Application::beginDragPlace 也拒绝 Material 类型资产；x64-debug 构建通过

- [x] TODO-005 【SceneSerializer】支持含 subMaterials 字段的 .ast 文件
  - 上下文：import 生成的 .ast 带有 subMaterials 数组，保存/加载场景时需要处理
  - 日期：2026-06-11
  - 完成日期：2026-06-17
  - 完成依据：SceneSerializer 保存/加载 subMaterialOverrides，加载入口 .ast 的 subMaterials，并放宽槽位数量边界；x64-debug 构建通过
