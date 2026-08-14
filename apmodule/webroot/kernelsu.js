// Minimal subset of the official KernelSU WebUI JavaScript bridge.
// Upstream: https://github.com/tiann/KernelSU/tree/main/js (Apache-2.0)
let callbackCounter = 0;

function callbackName(prefix) {
  return `${prefix}_callback_${Date.now()}_${callbackCounter++}`;
}

export function exec(command, options = {}) {
  return new Promise((resolve, reject) => {
    const name = callbackName("exec");
    window[name] = (errno, stdout, stderr) => {
      delete window[name];
      resolve({ errno, stdout, stderr });
    };
    try {
      globalThis.ksu.exec(command, JSON.stringify(options), name);
    } catch (error) {
      delete window[name];
      reject(error);
    }
  });
}

export function toast(message) {
  globalThis.ksu.toast(message);
}

export function moduleInfo() {
  return globalThis.ksu.moduleInfo();
}
