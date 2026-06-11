# 待办功能清单

> 记录今天想做但暂时搁置、留待以后实现的功能点。

---

## 记录格式

每个条目包含以下信息：

```
- [ ] 【描述】一句话概括功能点
  - 上下文：为什么想做 / 阻塞原因 / 依赖项
  - 日期：YYYY-MM-DD
```

---

## 待办

- [ ] 【Content Browser】新增文件夹功能，import 的 .ast 放在当前浏览的文件夹下
  - 上下文：目前所有 .ast 都平铺在 materials/ 下，无组织
  - 日期：2026-06-11

- [ ] 【Content Browser】新增 filter，根据 .ast 的 type 字段过滤显示
  - 上下文：Mesh 和 Material 类型混在一起，需要分类查看
  - 日期：2026-06-11

- [ ] 【.ast 格式】新增 "Material" 类型（type="Material"），不含 model 字段
  - 上下文：纯材质资产（不关联模型）需要独立表示；Material 类型的 ast 不能拖入场景
  - 日期：2026-06-11

- [ ] 【Content Browser】Material 类型的 .ast 禁止拖拽放置到场景
  - 上下文：Material ast 无 model 引用，拖拽无意义
  - 依赖：.ast 新增 Material 类型
  - 日期：2026-06-11

- [ ] 【SceneSerializer】支持含 subMaterials 字段的 .ast 文件
  - 上下文：import 生成的 .ast 带有 subMaterials 数组，保存/加载场景时需要处理
  - 日期：2026-06-11

---

## 已完成

_（暂无条目）_
