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

（当前为空）
---

## 待办

- [ ] TODO-006 【引擎/调试】支持 Agent 截图场景来调试和验证功能点
  - 上下文：从手动收集区整理；调试时需手动截图上传到外部 LLM 分析，流程繁琐。在 tinyEngine 中内置截图/帧捕获功能（如 RenderDoc 触发、离屏渲染到文件、或 Vulkan 帧缓冲导出），使 agent 可直接获取渲染画面进行分析和验证
  - 日期：2026-06-18
  - 来源：手动收集

- [ ] TODO-010 【Content Browser】添加删除资产文件功能，联动清理无引用的 bin payload
  - 上下文：从手动收集区整理；当前 Content Browser 无删除功能，废弃 .ast 文件需手动到文件系统删除。需求：在 Content Browser 中选中资产后提供删除按钮，删除 .ast 同时检查其引用的 bin/mesh、bin/texture、bin/anim 文件是否被其他 .ast 引用，若无引用则一并删除
  - 日期：2026-06-26
  - 来源：手动收集

- [ ] TODO-012 【动画压缩】加入 AI 压缩动画序列的功能
  - 上下文：当前动画序列占用空间较大，需要 AI 驱动的压缩方案减少资源体积
  - 日期：2026-06-26

- [ ] TODO-014 【测试准备】准备一套模型和动画用于测试动画状态机系统
  - 上下文：动画状态机系统开发需要测试素材验证功能正确性
  - 日期：2026-06-27

- [ ] TODO-015 【方案展示】与老师讨论展示动画状态机和 Sequencer 系统的方案
  - 上下文：需要对外展示阶段性成果，确定合适的演示方式和内容
  - 日期：2026-06-27

---

## 已完成

- [x] TODO-007 【动画/资产】将动画模型资产和动画序列资产分离为不同文件
  - 上下文：从手动收集区整理；当前动画数据随 glTF 一起加载，动画片段无独立文件格式存储和复用。参考 Phase A5 计划，需要实现 .anim.json 或 .anim 格式的独立动画资产文件，支持序列化/反序列化 AnimationClip
  - 日期：2026-06-18
  - 来源：手动收集
  - 完成日期：2026-06-28
  - 完成依据：Phase A5 的 AnimationAssetLoader 已实现 `.anim.ast` Header 与 `.anim.bin` 二进制序列化/反序列化；Mesh `.ast` 通过 `animations` 引用独立动画资产，glTF 与 FBX 导入均可生成并在运行时恢复 Skeleton/AnimationClip

- [x] TODO-013 【FBX 兼容】使项目兼容 FBX 格式的 model 和动画加载
  - 上下文：FBX 是业界通用格式，大量模型和动画资源为 FBX 格式，目前仅支持 glTF
  - 日期：2026-06-27
  - 完成日期：2026-06-28
  - 完成依据：以 Git Submodule 引入 ufbx；FbxImporter 已支持二进制/ASCII FBX 的网格、材质槽、PBR 参数、外部/内嵌纹理、蒙皮骨骼与烘焙动画解析，并接入 Content Browser、Mesh/Material/Anim 资产生成、SceneManager、ModelRegistry、缩略图与运行时缓存；x64-debug 构建及静态/材质/蒙皮动画 smoke tests 通过

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

- [x] TODO-008 【动画/加载】修复重新打开 scene 后 mesh 渲染异常（动画重复加载）
  - 上下文：手动收集区整理；重新打开 scene 后 mesh 渲染异常，但删掉重新拖拽出来恢复正常
  - 日期：2026-06-26
  - 来源：手动收集
  - 完成日期：2026-06-26
  - 完成依据：根因是迁移脚本将 .mesh.ast 的 subMaterials 重命名为 materials，但 MaterialAssetLoader 只读 subMaterials，导致 loadScene 跳过子材质加载。修复 MaterialAssetLoader::load 兼容 materials 字段（subMaterials 为空时回退）；x64-debug 构建通过，用户确认重新打开场景后 mesh 渲染正常

- [x] TODO-011 【引擎/构建】res 文件夹不再编译时复制，以项目根 res/ 为唯一基准
  - 上下文：手动收集区整理；此前 CMake post-build 把 res/ 复制到 exe 旁，导致保存的场景被源 res/ 覆盖，且存在双份 res 造成路径混乱
  - 日期：2026-06-26
  - 来源：手动收集
  - 完成日期：2026-06-26
  - 完成依据：重构 applicationResourceRoot() 从 exe 路径向上查找项目根（CMakeLists.txt + res/），所有 res 读写以项目根/res/ 为基准；移除 CMakeLists.txt 的 copy_directory post-build 步骤；x64-debug 构建通过

- [x] TODO-009 【Content Browser】新建文件夹应在当前浏览的子文件夹下创建
  - 上下文：手动收集区整理；当前 Create 按钮固定在 content/ 根下创建目录，未拼接 currentFolder_。用户在子文件夹浏览时点 Create，新文件夹应创建在当前子文件夹内而非 content 根
  - 日期：2026-06-26
  - 来源：手动收集
  - 完成日期：2026-06-26
  - 完成依据：IMGUIManager Create 按钮逻辑改为拼接 currentFolder_ + name 作为完整相对路径，create_directories 在 content/<currentFolder_>/<name> 下创建；currentFolder_ 为空时回退到 content/ 根；x64-debug 构建通过
