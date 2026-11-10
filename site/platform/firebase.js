// Provider operations only. Profile, save, retry and conflict decisions live in DynLex.
async function loadFirebase() {
  const [app,auth,store]=await Promise.all([
    import('https://www.gstatic.com/firebasejs/12.18.0/firebase-app.js'),
    import('https://www.gstatic.com/firebasejs/12.18.0/firebase-auth.js'),
    import('https://www.gstatic.com/firebasejs/12.18.0/firebase-firestore.js')
  ]);
  return {app,auth,store};
}

export function createFirebaseServices({load=loadFirebase,report=(operation,error)=>console.error(`Firebase ${operation}:`,error)}={}) {
  let connection=null,initializing=null,closed=false;
  const watchers=new Set();
  const check=signal=>{
    signal.throwIfAborted();
    if(closed) throw new DOMException('The provider is closed','AbortError');
  };
  async function initialize(configuration,signal) {
    const sdk=await load();check(signal);
    const app=sdk.app.initializeApp(configuration);
    let retained=false;
    try {
      const auth=sdk.auth.getAuth(app),store=sdk.store.getFirestore(app);
      await auth.authStateReady();check(signal);
      connection={sdk,app,auth,store};retained=true;
      return null;
    } finally {if(!retained) await sdk.app.deleteApp(app);}
  }
  function watch({signal,emit}) {
    const {sdk,auth}=connection;
    return new Promise(resolve=>{
      const unsubscribe=sdk.auth.onAuthStateChanged(auth,user=>{
        if(!closed&&!signal.aborted) emit({type:'firebase-auth',user:user===null?null:{uid:user.uid}});
      });
      const dispose=()=>{unsubscribe();watchers.delete(dispose);signal.removeEventListener('abort',dispose);resolve(null);};
      watchers.add(dispose);signal.addEventListener('abort',dispose,{once:true});
    });
  }
  function field(snapshot,name) {
    if(!snapshot.exists()) return null;
    const value=snapshot.get(name);
    if(typeof value!=='string') throw new Error('The selected document field must contain text');
    return value;
  }
  async function request(payload,context) {
    const {signal}=context;check(signal);
    if(payload.operation==='connect') {
      if(connection!==null||initializing!==null) throw new Error('The Firebase provider is already connected or connecting');
      initializing=initialize(payload.configuration,signal);
      try {return await initializing;} finally {initializing=null;}
    }
    if(connection===null) throw new Error('The Firebase provider is not connected');
    const {sdk,auth,store}=connection;
    if(payload.operation==='watch') return watch(context);
    if(payload.operation==='sign-in') {
      try {await sdk.auth.signInWithPopup(auth,new sdk.auth.GoogleAuthProvider());return {code:null};}
      catch(error) {
        if(typeof error?.code!=='string') throw error;
        report('sign-in',error);return {code:error.code};
      }
    }
    if(payload.operation==='sign-out') {await sdk.auth.signOut(auth);return null;}
    if(payload.operation==='read') {
      const snapshot=await sdk.store.getDocFromServer(sdk.store.doc(store,...payload.path));
      return {value:field(snapshot,payload.field)};
    }
    if(payload.operation==='compare-set') {
      if(payload.expected!==null&&typeof payload.expected!=='string') throw new Error('A document comparison requires text or null');
      return sdk.store.runTransaction(store,async transaction=>{
        const reference=sdk.store.doc(store,...payload.path),snapshot=await transaction.get(reference);
        if(field(snapshot,payload.field)!==payload.expected) return {matched:false};
        transaction.set(reference,{...payload.data,[payload.timestampField]:sdk.store.serverTimestamp()});
        return {matched:true};
      });
    }
    throw new Error(`Unknown Firebase operation: ${payload.operation}`);
  }
  async function close() {
    closed=true;
    for(const dispose of [...watchers]) dispose();
    // The connect caller owns its result; shutdown waits for resource cleanup.
    if(initializing!==null) await Promise.allSettled([initializing]);
    if(connection!==null) {
      const {sdk,app}=connection;connection=null;
      await sdk.app.deleteApp(app);
    }
  }
  return {services:{firebase:request},close};
}
