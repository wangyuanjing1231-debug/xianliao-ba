// 用《角色设定-全家.html》里的"爸爸"（带啤酒肚那版）画小智屏幕上的像素小人。
// 输出：frames/*.png（原始像素大小，透明底）+ poses.json + preview.png（放大的预览图）。
// 用法：npm install @napi-rs/canvas && node gen_frames.mjs
// 想换成自己的素材：直接替换 frames/ 里的 png（尺寸可以不同），改 poses.json，再跑 png2lvgl.py。

import { createCanvas } from "@napi-rs/canvas";
import fs from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";
import { drawChar, portrait, CAST } from "./family_sprites.mjs";

const HERE = path.dirname(fileURLToPath(import.meta.url));
const OUT = path.join(HERE, "frames");
fs.mkdirSync(OUT, { recursive: true });

const dad = CAST.find((c) => c.key === "dad");
const OUTFIT = dad.outfits.jacket; // 灰格子夹克配花纹毛衣（第一套）

const WHITE = "#f4f4f2", TEAL = "#5f9aa3", GREEN = "#3fae6a", YELLOW = "#f2c94c", BLUE = "#7fc4f0", GRAY = "#9aa0aa";
const R = (c, x, y, w, h, col) => { c.fillStyle = col; c.fillRect(x, y, w, h); };
const P = (c, pts, col) => { c.fillStyle = col; for (const [x, y] of pts) c.fillRect(x, y, 1, 1); };

function canvas(w, h) {
  const cv = createCanvas(w, h);
  const g = cv.getContext("2d");
  g.imageSmoothingEnabled = false;
  return [cv, g];
}

// 全身（16x26）
function body(dir, frame) {
  const [cv, g] = canvas(16, 26);
  drawChar(g, dad, OUTFIT, dir, frame);
  return cv;
}

// 头像（32x32），把纯色背景抠成透明
function face(ex) {
  const [cv, g] = canvas(32, 32);
  portrait(g, dad, OUTFIT, ex);
  const img = g.getImageData(0, 0, 32, 32);
  const bg = dad.pbg.slice(1).match(/../g).map((h) => parseInt(h, 16));
  for (let i = 0; i < img.data.length; i += 4) {
    if (img.data[i] === bg[0] && img.data[i + 1] === bg[1] && img.data[i + 2] === bg[2]) img.data[i + 3] = 0;
  }
  g.putImageData(img, 0, 0);
  return cv;
}

const CANVAS = {};
function save(name, cv) {
  fs.writeFileSync(path.join(OUT, name + ".png"), cv.toBuffer("image/png"));
  CANVAS[name + ".png"] = cv;
  return name + ".png";
}

const poses = {};

// ---------- 待机：全身站着，偶尔眨眼 ----------
{
  const frames = [];
  for (const blink of [false, true]) {
    const [cv, g] = canvas(28, 30);
    R(g, 8, 28, 12, 1, "rgba(0,0,0,0.28)"); // 脚下影子
    g.drawImage(body("down", 0), 6, 3);
    if (blink) P(g, [[12, 10], [15, 10]], dad.skin); // 眼睛那两个像素涂成肤色 = 闭眼
    frames.push(save(`idle_${blink ? 1 : 0}`, cv));
  }
  poses.idle = { scale: 6, frames: [frames[0], frames[1], frames[0]], ms: [2600, 160, 1800] };
}

// ---------- 聆听：头像睁大眼 + 耳边声波 ----------
{
  const frames = [];
  for (let n = 1; n <= 3; n++) {
    const [cv, g] = canvas(40, 34);
    g.drawImage(face("surprised"), 2, 2);
    if (n >= 1) P(g, [[33, 14], [34, 15], [34, 16], [34, 17], [33, 18]], TEAL);
    if (n >= 2) P(g, [[35, 12], [36, 13], [37, 14], [37, 15], [37, 16], [37, 17], [37, 18], [36, 19], [35, 20]], TEAL);
    if (n >= 3) P(g, [[37, 10], [38, 11], [39, 12], [39, 13], [39, 14], [39, 15], [39, 16], [39, 17], [39, 18], [39, 19], [39, 20], [38, 21], [37, 22]], TEAL);
    frames.push(save(`listen_${n}`, cv));
  }
  poses.listen = { scale: 5, frames: [frames[0], frames[1], frames[2], frames[1]], ms: [260, 260, 260, 260] };
}

// ---------- 思考：头像抿嘴 + 头顶冒点点 ----------
{
  const frames = [];
  for (let n = 1; n <= 3; n++) {
    const [cv, g] = canvas(40, 34);
    const f = face("normal");
    g.drawImage(f, 2, 2);
    R(g, 15, 21, 6, 1, dad.skin);              // 盖掉咧嘴
    R(g, 16, 21, 4, 1, dad.mouth);             // 抿成一条线
    const dots = [[28, 6], [32, 3], [36, 1]];
    for (let i = 0; i < n; i++) R(g, dots[i][0], dots[i][1], 2, 2, WHITE);
    frames.push(save(`think_${n}`, cv));
  }
  poses.think = { scale: 5, frames: [frames[0], frames[1], frames[2], frames[2]], ms: [320, 320, 320, 320] };
}

// ---------- 说话：头像嘴巴一张一合 ----------
{
  const mouths = {
    grin: null, // 头像本来的咧嘴
    open: (g) => { R(g, 15, 21, 6, 2, "#8a3a3a"); R(g, 16, 21, 4, 1, "#ffffff"); },
    o: (g) => { R(g, 15, 21, 6, 1, dad.skin); R(g, 17, 21, 2, 2, "#8a3f3f"); },
  };
  const frames = [];
  for (const [k, fn] of Object.entries(mouths)) {
    const [cv, g] = canvas(40, 34);
    g.drawImage(face("normal"), 2, 2);
    if (fn) fn(g);
    if (k !== "grin") P(g, [[34, 20], [35, 20], [36, 19], [34, 23], [35, 23], [36, 24]], WHITE); // 嘴边的"话"
    frames.push(save(`speak_${k}`, cv));
  }
  poses.speak = { scale: 5, frames: [frames[0], frames[1], frames[2], frames[1]], ms: [170, 170, 170, 170] };
}

// ---------- 干活中：全身侧身走路，抱着一叠资料，冒汗 ----------
{
  const frames = [];
  for (const [i, f] of [1, 0, 2, 0].entries()) {
    const [cv, g] = canvas(28, 30);
    R(g, 8, 28, 12, 1, "rgba(0,0,0,0.28)");
    g.drawImage(body("right", f), 6, 3);
    const bob = f === 0 ? 0 : 1;
    R(g, 17, 15 + bob, 5, 6, WHITE);                       // 一叠纸
    R(g, 18, 16 + bob, 3, 1, GRAY); R(g, 18, 18 + bob, 3, 1, GRAY);
    R(g, 17, 21 + bob, 5, 1, "#c9ccd2");
    if (i % 2 === 0) P(g, [[20, 4], [20, 5], [21, 5]], BLUE); // 汗珠
    frames.push(save(`work_${i}`, cv));
  }
  poses.work = { scale: 6, frames, ms: [180, 180, 180, 180] };
}

// ---------- 干完了：全身正面，头顶绿色对勾 + 闪光 ----------
{
  const frames = [];
  for (const k of [0, 1]) {
    const [cv, g] = canvas(28, 30);
    R(g, 8, 28, 12, 1, "rgba(0,0,0,0.28)");
    g.drawImage(body("down", 0), 6, 3);
    // 对勾徽章
    R(g, 20, 0, 7, 7, GREEN);
    g.clearRect(20, 0, 1, 1); g.clearRect(26, 0, 1, 1); g.clearRect(20, 6, 1, 1); g.clearRect(26, 6, 1, 1);
    P(g, [[21, 3], [22, 4], [23, 5], [24, 4], [25, 3], [25, 2]], WHITE);
    // 闪光，两帧换位置
    const sp = k === 0 ? [[2, 6], [4, 14]] : [[3, 10], [23, 12]];
    for (const [x, y] of sp) P(g, [[x, y - 1], [x - 1, y], [x, y], [x + 1, y], [x, y + 1]], YELLOW);
    frames.push(save(`done_${k}`, cv));
  }
  poses.done = { scale: 6, frames, ms: [520, 520] };
}

// ---------- 出错/没连上：头像难过 ----------
{
  const [cv, g] = canvas(40, 34);
  g.drawImage(face("sad"), 2, 2);
  R(g, 31, 2, 7, 3, GRAY); R(g, 30, 3, 9, 2, GRAY);         // 小乌云
  P(g, [[32, 7], [35, 8], [37, 6]], BLUE);                   // 雨点
  poses.sad = { scale: 5, frames: [save("sad_0", cv)], ms: [1000] };
}

fs.writeFileSync(path.join(HERE, "poses.json"), JSON.stringify(poses, null, 2));

// ---------- 预览图：每个状态一行，按设备上的放大倍数画（屏幕 240x240） ----------
{
  const names = { idle: "idle", listen: "listen", think: "think", speak: "speak", work: "working", done: "done", sad: "error" };
  const rows = Object.keys(poses);
  const cell = 240, gap = 8, W = 130 + (cell + gap) * 4, H = (cell + gap) * rows.length;
  const [cv, g] = canvas(W, H);
  R(g, 0, 0, W, H, "#15161b");
  g.font = "26px sans-serif";
  rows.forEach((pose, r) => {
    const y0 = r * (cell + gap);
    g.fillStyle = "#e8e6e1";
    g.fillText(names[pose], 12, y0 + 120);
    const p = poses[pose];
    [...new Set(p.frames)].slice(0, 4).forEach((file, i) => {
      const x0 = 130 + i * (cell + gap);
      R(g, x0, y0, cell, cell, "#000000");              // 设备屏幕底色（深色主题）
      const src = CANVAS[file];
      const w = src.width * p.scale, h = src.height * p.scale;
      g.drawImage(src, x0 + (cell - w) / 2, y0 + 30 + (cell - 30 - h) / 2, w, h);
      R(g, x0, y0, cell, 26, "#202228");                // 顶部状态栏位置
    });
  });
  fs.writeFileSync(path.join(HERE, "preview.png"), cv.toBuffer("image/png"));
}
console.log("poses:", Object.keys(poses).map((k) => `${k}(${poses[k].frames.length})`).join(" "));
