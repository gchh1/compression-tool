#import "@preview/fletcher:0.5.8" as fletcher: diagram, node, edge

#let project(title: "", author: "", body) = {
  // 设置文档元数据
  set document(title: title, author: author)
  

  // 设置页面布局
  set page(
    paper: "a4",
    margin: (x: 2cm, y: 2.5cm),
    header: align(right, text(8pt, gray)[#title]),
    footer: context counter(page).display("1 / 1", both: true),
  )
  // 设置文本与段落
  set text(font: ("Comic Sans MS", "Source Han Serif SC"), size: 11pt)
  set par(justify: true, first-line-indent: (amount: 2em, all: true), leading: 1.5em)
  show par: set block(spacing: 1.8em)
  
  // 设置列表
  set list(spacing: 1.2em, indent: 1em)
  set enum(spacing: 1.2em, indent: 1.2em)

  // 设置标题样式
  show heading: set block(above: 2em, below: 1em)
  show heading.where(level: 1): it => [
    #set text(18pt, weight: "bold")
    #stack(dir: ltr, spacing: 0.5em, it.body)
  ]

  show heading.where(level: 2): it => [
    #set text(16pt, weight: "bold")
    #pad(left: 0.2em,
    stack(dir: ltr, spacing: 0.5em, it.body))
  ] 

  show heading.where(level: 3): it => [
    #set text(16pt, weight: "bold")
    #pad(left: 2.5em,
    stack(dir: ltr, spacing: 0.5em, it.body))
  ] 
  body
}

// 自定义：知识点/提示框
#let note(caption: "注意", body) = {
  rect(
    width: 100%,
    inset: 10pt,
    fill: rgb("#eefaff"),
    stroke: (left: 3pt + rgb("#007acc")),
    radius: 2pt
  )[
    #text(weight: "bold", fill: rgb("#007acc"))[#caption] \
    #body
  ]
}

/**
 * 插入图片指令
 * @param path 图片路径
 * @param caption 图片标题，默认为 none
 * @param width 图片宽度，默认为 80%
 * @param label 图片标签，默认为 none
 * @param supplement 图片补充说明，默认为 [图]
*/
#let myFigure(
  path, 
  caption: none, 
  width: 80%, 
  label: none, 
  supplement: [图]
) = {
  figure(
    image(path, width: width),
    caption: caption,
    supplement: supplement,
    kind: image,
  ) + if label != none { label }
}

/**
 * 代码块指令
 * @param body 代码内容
 * @param lang 代码语言，默认为 none
 * @param title 代码标题，默认为 none
*/
#let code(
  body, 
  lang: none, 
  title: none, 
  fill: rgb("#f5f5f5"), 
  stroke: 0.5pt + rgb("#d1d1d1"),
  indent: 2em
) = {
    block(
    width: 100%,
    fill: fill,
    stroke: stroke,
    radius: 4pt,
    clip: true,
    stack(
      // 标题栏部分
      if title != none or lang != none {
        block(
          width: 100%,
          inset: (x: 8pt, y: 6pt),
          fill: rgb("#e8e8e8"),
          stroke: (bottom: stroke),
          {
            if title != none { strong(title) }
            if lang != none { h(1fr); text(style: "italic", fill: gray.darken(20%), lang) }
          }
        )
      },
      // 代码主体部分
      block(
        inset: 12pt,
        width: 100%,
        // 强制 body 中的 raw 元素不显示默认背景，由外层 block 提供
        {
          show raw: set block(fill: none, inset: (x: 3pt, y: 1pt), radius: 0pt)
          body
        }
      )
    )
  )
}


#let th(content) = text(fill: white)[#content] 

#let rd(content) = text(fill: red)[#content]

#let space(num) = {
    for i in range(num) {
      sym.space.nobreak
  }
}

 
#let set_node(point, body, color: rgb("#4b7deb")) = {
  node(point, text(fill: white, weight: "bold", body),fill: color, stroke: black, inset: 10pt)
}