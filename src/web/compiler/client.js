// Worker transport. Cancellation terminates computation, including synchronous
// WebAssembly, and rejects every pending request owned by that worker.
export class WorkerConnection {
  constructor(url, closed = () => {}) {
    this.worker = new Worker(url, { type: 'module' });
    this.pending = new Map(); this.sequence = 0; this.closed = false; this.ready = false;
    this.onClose = closed;
    this.worker.onmessage = ({ data }) => {
      if (data.ready === true) {
        if (this.ready) throw new Error('The worker initialized twice');
        this.ready = true;
        // Module dependencies can await before the worker installs onmessage.
        // Only this explicit boundary makes an initial request deliverable.
        for (const request of this.pending.values()) this.worker.postMessage(request.message, request.transfer);
        return;
      }
      const request = this.pending.get(data.id);
      if (request === undefined) throw new Error('Worker replied to an unknown request');
      if (Object.hasOwn(data, 'event')) { request.event(data.event); return; }
      this.pending.delete(data.id); request.dispose();
      if (Object.hasOwn(data, 'error')) {
        request.reject(new Error(data.error));
        if (data.fatal) this.close(new Error(data.error));
      }
      else request.resolve(data.result);
    };
    this.worker.onerror = event => {
      console.error('DynLex worker:', event.message);
      this.close(new Error('An error occurred. Check the log.'));
    };
  }
  request(type, payload, { signal, event = () => {}, transfer = [] } = {}) {
    if (this.closed) return Promise.reject(new Error('The worker connection is closed'));
    if (signal?.aborted) return Promise.reject(signal.reason);
    return new Promise((resolve, reject) => {
      const id = ++this.sequence;
      const abort = () => this.close(signal.reason);
      signal?.addEventListener('abort', abort, { once: true });
      const message = { id, type, payload };
      this.pending.set(id, { resolve, reject, event, message, transfer,
        dispose: () => signal?.removeEventListener('abort', abort) });
      if (this.ready) this.worker.postMessage(message, transfer);
    });
  }
  close(reason = new DOMException('The worker was closed', 'AbortError')) {
    if (this.closed) return;
    this.closed = true; this.worker.terminate();
    for (const request of this.pending.values()) { request.dispose(); request.reject(reason); }
    this.pending.clear(); this.onClose();
  }
}
