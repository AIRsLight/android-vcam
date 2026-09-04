import { exec, moduleInfo, toast } from "./kernelsu.js";

const FRAME_WIDTH = 576;
const FRAME_HEIGHT = 324;
const encoder = new TextEncoder();
let providers = [];
let selectedImage = null;
let selectedImageName = "";
let objectUrl = null;
let sequence = Math.floor(Date.now() / 1000) >>> 0;

const ui = Object.fromEntries([
  "refresh", "statusDetail", "cameraCount", "objectCount", "providerList",
  "providerName", "providerType", "providerSource", "sourceRow", "fileRow",
  "mediaPicker", "previewShell", "preview", "addProvider", "providerOperation",
  "routePackage", "routeTarget", "routeProvider", "packageList", "setRoute",
  "routeList",
].map(id => [id, document.querySelector(`#${id}`)]));
const context = ui.preview.getContext("2d", { alpha: false, willReadFrequently: true });

function controllerPath() {
  try {
    const info = JSON.parse(moduleInfo());
    if (info.moduleDir) return `${info.moduleDir}/vcamctl`;
  } catch (_) { }
  return "/data/adb/modules/android_vcam/vcamctl";
}

function shellQuote(value) {
  return `'${String(value).replaceAll("'", `'\\''`)}'`;
}

async function ctl(...args) {
  const command = [controllerPath(), ...args].map(shellQuote).join(" ");
  const result = await exec(command);
  if (result.errno !== 0) {
    throw new Error((result.stderr || result.stdout || `exit ${result.errno}`).trim());
  }
  return result.stdout || "";
}

function parseStatus(text) {
  const result = {};
  for (const line of text.split(/\r?\n/)) {
    const split = line.indexOf("=");
    if (split > 0) result[line.slice(0, split)] = line.slice(split + 1);
  }
  return result;
}

function decode64(text) {
  if (!text) return "";
  try {
    const binary = atob(text);
    return new TextDecoder().decode(Uint8Array.from(binary, value => value.charCodeAt(0)));
  } catch (_) { return text; }
}

function encode64(bytes) {
  let binary = "";
  for (let offset = 0; offset < bytes.length; offset += 0x8000) {
    binary += String.fromCharCode(...bytes.subarray(offset, offset + 0x8000));
  }
  return btoa(binary);
}

function parseProviders(text) {
  return text.split(/\r?\n/).filter(Boolean).map(line => {
    const [kind, id, type, name, source, removable, running] = line.split("\t");
    if (kind !== "PROVIDER") return null;
    return { id, type, name: decode64(name), source: decode64(source), removable: removable === "true", running: running === "true" };
  }).filter(Boolean);
}

function parseRoutes(text) {
  return text.split(/\r?\n/).filter(Boolean).map(line => {
    const [kind, packageName, target, provider] = line.split("\t");
    return kind === "ROUTE" ? { packageName, target, provider } : null;
  }).filter(Boolean);
}

function setBoolean(selector, enabled, yes, no) {
  const element = document.querySelector(selector);
  element.textContent = enabled ? yes : no;
  element.className = enabled ? "good" : "bad";
}

function typeLabel(type) {
  return ({ physical: "物理相机", pattern: "内置彩条", image: "静态图片", video: "本地视频", http: "HTTP", https: "HTTPS 视频", hls: "HLS", rtsp: "RTSP" })[type] || type;
}

function actionButton(label, className, handler) {
  const button = document.createElement("button");
  button.type = "button";
  button.className = `small ${className || ""}`.trim();
  button.textContent = label;
  button.addEventListener("click", async () => {
    button.disabled = true;
    try { await handler(); } catch (error) { toast(error.message || String(error)); }
    finally { button.disabled = false; }
  });
  return button;
}

function renderProviders() {
  ui.providerList.replaceChildren();
  ui.providerList.classList.toggle("empty", providers.length === 0);
  for (const provider of providers) {
    const item = document.createElement("div");
    item.className = "item";
    const body = document.createElement("div");
    const title = document.createElement("h3");
    title.textContent = provider.name || provider.id;
    const badge = document.createElement("span");
    badge.className = `badge ${provider.running ? "on" : ""}`;
    badge.textContent = provider.running ? "运行中" : "已停止";
    title.append(badge);
    const detail = document.createElement("p");
    detail.textContent = `${provider.id} · ${typeLabel(provider.type)}${provider.source ? ` · ${provider.source}` : ""}`;
    body.append(title, detail);
    const actions = document.createElement("div");
    actions.className = "item-actions";
    if (!provider.removable) {
      const lock = document.createElement("span");
      lock.className = "badge on";
      lock.textContent = "固定";
      actions.append(lock);
    } else {
      actions.append(actionButton(provider.running ? "停止" : "启动", "quiet", async () => {
        await ctl(provider.running ? "provider-stop" : "provider-start", provider.id);
        await refreshAll();
      }));
      actions.append(actionButton("删除", "danger", async () => {
        if (!confirm(`删除提供器“${provider.name || provider.id}”及其全部路由？`)) return;
        await ctl("provider-remove", provider.id);
        toast("提供器已删除");
        await refreshAll();
      }));
    }
    item.append(body, actions);
    ui.providerList.append(item);
  }

  const previous = ui.routeProvider.value;
  ui.routeProvider.replaceChildren();
  for (const provider of providers) {
    const option = document.createElement("option");
    option.value = provider.id;
    option.textContent = `${provider.name || provider.id}${provider.running ? "" : "（已停止）"}`;
    ui.routeProvider.append(option);
  }
  if (providers.some(provider => provider.id === previous)) ui.routeProvider.value = previous;
}

function renderRoutes(routes) {
  ui.routeList.replaceChildren();
  ui.routeList.classList.toggle("empty", routes.length === 0);
  if (routes.length === 0) {
    ui.routeList.textContent = "尚未配置路由，所有应用均使用各自的物理相机。";
    return;
  }
  for (const route of routes) {
    const item = document.createElement("div");
    item.className = "item";
    const body = document.createElement("div");
    const title = document.createElement("h3");
    title.textContent = route.packageName;
    const detail = document.createElement("p");
    const provider = providers.find(candidate => candidate.id === route.provider);
    detail.textContent = `目标相机 ${route.target} → ${provider?.name || route.provider}`;
    body.append(title, detail);
    const remove = actionButton("移除", "danger", async () => {
      await ctl("route-remove", route.packageName, route.target);
      toast("路由已移除");
      await refreshAll();
    });
    item.append(body, remove);
    ui.routeList.append(item);
  }
}

async function refreshAll() {
  ui.refresh.disabled = true;
  try {
    const [statusText, providerText, routeText] = await Promise.all([
      ctl("status"), ctl("providers"), ctl("routes"),
    ]);
    const status = parseStatus(statusText);
    providers = parseProviders(providerText);
    const routes = parseRoutes(routeText);
    setBoolean("#moduleState", status.module_enabled === "true", "已启用", "已禁用");
    setBoolean("#mountState", status.mount_active === "true", "已挂载", "未确认");
    ui.cameraCount.textContent = status.camera_devices || "—";
    ui.objectCount.textContent = `${routes.length} / ${providers.length}`;
    ui.statusDetail.textContent = `HAL ${status.hal_hash ? status.hal_hash.slice(0, 12) : "未知"}…`;
    renderProviders();
    renderRoutes(routes);
  } catch (error) {
    ui.statusDetail.textContent = `读取失败：${error.message || error}`;
  } finally { ui.refresh.disabled = false; }
}

function drawImage(image) {
  ui.preview.width = FRAME_WIDTH;
  ui.preview.height = FRAME_HEIGHT;
  context.fillStyle = "#000";
  context.fillRect(0, 0, FRAME_WIDTH, FRAME_HEIGHT);
  const scale = Math.min(FRAME_WIDTH / image.naturalWidth, FRAME_HEIGHT / image.naturalHeight);
  const width = Math.max(1, Math.round(image.naturalWidth * scale));
  const height = Math.max(1, Math.round(image.naturalHeight * scale));
  context.drawImage(image, (FRAME_WIDTH - width) / 2, (FRAME_HEIGHT - height) / 2, width, height);
}

function currentFrame() {
  const rgba = context.getImageData(0, 0, FRAME_WIDTH, FRAME_HEIGHT).data;
  const payloadLength = FRAME_WIDTH * FRAME_HEIGHT * 3;
  const bytes = new Uint8Array(24 + payloadLength);
  bytes.set(encoder.encode("VCAMRGB1"));
  const header = new DataView(bytes.buffer);
  header.setUint32(8, FRAME_WIDTH, true);
  header.setUint32(12, FRAME_HEIGHT, true);
  header.setUint32(16, payloadLength, true);
  header.setUint32(20, ++sequence, true);
  for (let source = 0, target = 24; source < rgba.length; source += 4) {
    bytes[target++] = rgba[source]; bytes[target++] = rgba[source + 1]; bytes[target++] = rgba[source + 2];
  }
  return bytes;
}

async function publishFrameInChunks(providerId, bytes) {
  const encoded = encode64(bytes);
  const token = `web-${Date.now().toString(36)}`;
  const chunkSize = 32768;
  await ctl("provider-publish-begin", providerId, token);
  try {
    for (let offset = 0; offset < encoded.length; offset += chunkSize) {
      await ctl("provider-publish-chunk", providerId, token,
        encoded.slice(offset, offset + chunkSize));
    }
    await ctl("provider-publish-commit", providerId, token);
  } catch (error) {
    await ctl("provider-publish-abort", providerId, token).catch(() => {});
    throw error;
  }
}

function updateProviderForm() {
  const type = ui.providerType.value;
  const remote = ["video", "http", "https", "hls", "rtsp"].includes(type);
  ui.sourceRow.classList.toggle("hidden", !remote);
  ui.fileRow.classList.toggle("hidden", type !== "image");
  ui.previewShell.classList.toggle("hidden", type !== "image" || !selectedImage);
  ui.providerOperation.textContent = type === "pattern" ? "彩条由 HAL 直接生成，不占用解码器。" :
    type === "image" ? "图片会转换为 576×324 RGB 帧并保存在该提供器中。" :
    type === "https" ? "HTTPS 视频先通过 Android 系统 TLS 下载到模块私有缓存，再循环解码。" :
    "FFmpeg 在后台持续解码，断流后会自动重连。";
}

ui.mediaPicker.addEventListener("change", () => {
  const file = ui.mediaPicker.files?.[0];
  if (!file) return;
  if (objectUrl) URL.revokeObjectURL(objectUrl);
  objectUrl = URL.createObjectURL(file);
  const image = new Image();
  image.onload = () => {
    selectedImage = image;
    selectedImageName = file.name || "静态图片";
    drawImage(image);
    ui.previewShell.classList.remove("hidden");
  };
  image.onerror = () => { selectedImage = null; ui.providerOperation.textContent = "无法解码所选图片。"; };
  image.src = objectUrl;
});

ui.addProvider.addEventListener("click", async () => {
  const type = ui.providerType.value;
  const name = (ui.providerName.value || selectedImageName || typeLabel(type)).trim();
  const source = ui.providerSource.value.trim();
  if (!name) { toast("请填写提供器名称"); return; }
  if (["video", "http", "https", "hls", "rtsp"].includes(type) && !source) { toast("请填写视频地址或路径"); return; }
  if (type === "image" && !selectedImage) { toast("请先选择图片"); return; }
  const id = `p-${Date.now().toString(36)}`;
  ui.addProvider.disabled = true;
  try {
    await ctl("provider-add", id, type, encode64(encoder.encode(name)), encode64(encoder.encode(source)));
    try {
      if (type === "image") await publishFrameInChunks(id, currentFrame());
      else await ctl("provider-start", id);
    } catch (error) {
      await ctl("provider-remove", id).catch(() => {});
      throw error;
    }
    ui.providerName.value = "";
    ui.providerSource.value = "";
    toast("提供器已添加");
    await refreshAll();
  } catch (error) { ui.providerOperation.textContent = `添加失败：${error.message || error}`; }
  finally { ui.addProvider.disabled = false; }
});

ui.setRoute.addEventListener("click", async () => {
  const packageName = ui.routePackage.value.trim();
  if (!/^[A-Za-z0-9_.-]{1,255}$/.test(packageName)) { toast("包名格式无效"); return; }
  ui.setRoute.disabled = true;
  try {
    await ctl("route-set", packageName, ui.routeTarget.value, ui.routeProvider.value);
    toast("路由已保存，新相机会话开始时生效");
    await refreshAll();
  } catch (error) { toast(error.message || String(error)); }
  finally { ui.setRoute.disabled = false; }
});

async function loadApps() {
  try {
    const packages = (await ctl("apps")).split(/\r?\n/).filter(Boolean);
    const fragment = document.createDocumentFragment();
    for (const packageName of packages) {
      const option = document.createElement("option"); option.value = packageName; fragment.append(option);
    }
    ui.packageList.replaceChildren(fragment);
  } catch (_) { }
}

ui.providerType.addEventListener("change", updateProviderForm);
ui.refresh.addEventListener("click", refreshAll);
window.addEventListener("beforeunload", () => { if (objectUrl) URL.revokeObjectURL(objectUrl); });
updateProviderForm();
refreshAll();
loadApps();
