// Runtime imports use top-level await. Requests must wait until the worker has
// installed its handler; starting a Worker does not establish that boundary.
await new Promise(resolve => setTimeout(resolve, 80));
self.onmessage = ({ data }) => self.postMessage({ id: data.id, result: data.payload });
self.postMessage({ ready: true });
