// tinyjs client shim. window.__invoke is the native bound function the
// launcher injects; it already returns a promise resolved by the backend.
const api = {
  call(method, params) {
    return window.__invoke(JSON.stringify({ method, params }));
  },
  _handlers: {},
  on(event, fn) {
    (this._handlers[event] ||= []).push(fn);
  },
};
window.__emit = (msg) => {
  (api._handlers[msg.event] || []).forEach((fn) => fn(msg.data));
};
window.api = api;
