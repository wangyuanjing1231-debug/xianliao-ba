// OpenClaw 插件：让科室小智开口。
// 1) 工具 xiaozhi_say：智能体（比如收到微信/飞书消息时）可以调用它，让小智把一句话念出来。
// 2) 命令 /xiaozhi 文字：不经过大模型，原样转给小智念（最稳的用法）。
// 不引用 OpenClaw 内部模块，只用插件 API 的 registerTool / registerCommand，升级不容易坏。

const ID = "xiaozhi-voice";
const KEYS = ["serverUrl", "apiToken", "defaultDevice", "maxChars"];

const configSchema = {
  jsonSchema: {
    type: "object",
    additionalProperties: false,
    properties: {
      serverUrl: { type: "string" },
      apiToken: { type: "string" },
      defaultDevice: { type: "string" },
      maxChars: { type: "integer", minimum: 20, maximum: 600 },
    },
  },
  safeParse(value) {
    if (value === undefined) return { success: true, data: undefined };
    if (!value || typeof value !== "object" || Array.isArray(value)) {
      return { success: false, error: { issues: [{ path: [], message: "expected config object" }] } };
    }
    for (const k of Object.keys(value)) {
      if (!KEYS.includes(k)) {
        return { success: false, error: { issues: [{ path: [k], message: `unknown key ${k}` }] } };
      }
    }
    return { success: true, data: value };
  },
};

function settings(api) {
  const c = (api && api.pluginConfig) || {};
  return {
    serverUrl: String(c.serverUrl || process.env.XIAOZHI_SERVER_URL || "http://127.0.0.1:8000").replace(/\/+$/, ""),
    apiToken: String(c.apiToken || process.env.XIAOZHI_API_TOKEN || ""),
    defaultDevice: String(c.defaultDevice || "all"),
    maxChars: Number(c.maxChars || 300),
  };
}

async function speak(api, text, device, source) {
  const s = settings(api);
  const body = String(text || "").trim().slice(0, s.maxChars);
  if (!body) return { ok: false, error: "没有要念的内容" };
  if (!s.apiToken) return { ok: false, error: "插件没配置 apiToken" };
  try {
    const res = await fetch(`${s.serverUrl}/api/push`, {
      method: "POST",
      headers: { "Content-Type": "application/json", Authorization: `Bearer ${s.apiToken}` },
      body: JSON.stringify({ text: body, device: device || s.defaultDevice, source }),
      signal: AbortSignal.timeout(10000),
    });
    const data = await res.json().catch(() => ({}));
    if (!res.ok) return { ok: false, error: `小智服务端返回 ${res.status}` };
    return data;
  } catch (err) {
    return { ok: false, error: `连不上小智服务端：${err && err.message ? err.message : err}` };
  }
}

export default {
  id: ID,
  name: "Xiaozhi Voice",
  description: "让科室的小智语音设备开口说话",
  configSchema,
  register(api) {
    api.registerTool({
      name: "xiaozhi_say",
      label: "让小智念出来",
      // 常驻在模型眼前，不藏进"工具搜索"目录：国产模型也能稳定调到
      catalogMode: "direct-only",
      description:
        "Speak a short message out loud on the department's Xiaozhi desk voice device (a small speaker in the office). " +
        "Use it when the user asks you to announce, read out, or remind people in the office by voice (e.g. 让小智说/让小智提醒大家/念给科室听). " +
        "Write the text as plain spoken Chinese: one to three short sentences, no markdown, no emoji, no links.",
      parameters: {
        type: "object",
        additionalProperties: false,
        properties: {
          text: { type: "string", description: "要念出来的话（口语，一到三句）" },
          device: { type: "string", description: "可选：设备 MAC；不填就念给所有小智" },
        },
        required: ["text"],
      },
      async execute(_toolCallId, params) {
        const result = await speak(api, params && params.text, params && params.device, "tool");
        const text = result.ok
          ? `已交给小智朗读（设备：${(result.devices || []).join(", ") || "无"}）。设备忙时会排队，空下来就念。`
          : `没念成：${result.error || "未知错误"}`;
        return { content: [{ type: "text", text }], details: result };
      },
    });

    api.registerCommand({
      name: "xiaozhi",
      description: "让科室小智原样念一句话：/xiaozhi 下午三点开会",
      acceptsArgs: true,
      handler: async (ctx) => {
        const text = String((ctx && ctx.args) || "").trim();
        if (!text) return { text: "用法：/xiaozhi 要念的话" };
        const result = await speak(api, text, "", `command:${(ctx && ctx.channel) || "?"}`);
        return { text: result.ok ? "好的，小智会念出来。" : `没念成：${result.error || "未知错误"}` };
      },
    });

    if (api.logger && api.logger.info) {
      api.logger.info(`[${ID}] ready, server=${settings(api).serverUrl}`);
    }
  },
};
