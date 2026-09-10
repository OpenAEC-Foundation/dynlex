import { createProgressStore, emptyProgress, mergeProgress } from "./game-progress.js";
import { firebaseConfig } from "./firebase-config.js";

export function initializeGameAccount(section) {
  const bar=document.createElement("div");bar.className="game-account";
  bar.innerHTML='<span data-save-status role="status">Progress saves in this browser</span><button type="button" data-game-resume hidden>CONTINUE GAME</button><button type="button" data-google-signin hidden>Sign in with Google</button><button type="button" data-google-signout hidden>Sign out</button>';
  section.querySelector(".section-title").after(bar);
  const status=bar.querySelector("[data-save-status]"),login=bar.querySelector("[data-google-signin]"),logout=bar.querySelector("[data-google-signout]");
  const resume=bar.querySelector("[data-game-resume]");
  const report = error => {console.error("Game progress failed",error);status.textContent="An error occurred. Check the browser log.";};
  const progress=createProgressStore(localStorage,report);
  let cloud=null,user=null,ready=false,timer=null,writing=false,dirty=false,stopped=false,authGeneration=0;
  const reload = () => {progress.suspend();location.reload();};
  function showResume() {resume.hidden=progress.snapshot.lastGame===null;}
  showResume();
  async function sync() {
    clearTimeout(timer);timer=null;
    if(!ready || !user || writing || !dirty || stopped) return;
    writing=true;
    const snapshot=progress.snapshot,uid=user.uid;
    status.textContent="Saving to your account…";
    try {
      await cloud.write(uid,snapshot);
      if(user?.uid!==uid) return;
      dirty=JSON.stringify(snapshot)!==JSON.stringify(progress.snapshot);
      status.textContent=dirty ? "Saved in this browser · syncing" : "Progress saved to your account";
    } catch(error) {
      if(user?.uid!==uid) return;
      if(error.code==="save-conflict") {
        stopped=true;
        status.textContent="Another device has newer progress.";
        const button=document.createElement("button");button.textContent="LOAD ACCOUNT SAVE";
        button.onclick=async()=>{
          button.disabled=true;
          try {
            const remote=await cloud.read(user.uid);
            localStorage.setItem(`dynlex.games.backup:${user.uid}`,JSON.stringify(progress.snapshot));
            progress.replace(remote);reload();
          }
          catch(error) {report(error);button.disabled=false;}
        };
        const keep=document.createElement("button");keep.textContent="KEEP THIS GAME";
        keep.onclick=async()=>{
          keep.disabled=true;
          try {await cloud.read(user.uid);stopped=false;button.remove();keep.remove();await sync();}
          catch(error) {report(error);keep.disabled=false;}
        };
        bar.append(button,keep);
      } else report(error);
    } finally {
      writing=false;
      if(dirty && !stopped) timer=setTimeout(()=>void sync(),10000);
    }
  }
  progress.subscribe(reason=>{
    showResume();
    if(reason!=="save") return;
    dirty=true;
    if(ready && user) {
      if(timer===null) timer=setTimeout(()=>void sync(),10000);
    } else status.textContent="Progress saved in this browser";
  });
  window.addEventListener("online",()=>{
    if(cloud && !ready) void useAccount(cloud.user).catch(report);
    else void sync();
  });
  document.addEventListener("visibilitychange",()=>{if(document.hidden) void sync();});
  async function useAccount(nextUser) {
    const generation=++authGeneration;
    ready=false;
    if(nextUser===null) {
      user=null;login.hidden=false;logout.hidden=true;
      if(progress.profile!=="guest") {progress.suspend();progress.switchProfile("guest");reload();}
      else status.textContent="Progress saves in this browser";
      ready=true;return;
    }
    user=nextUser;login.hidden=true;logout.hidden=false;
    status.textContent="Loading your saved progress…";
    const remote=await cloud.read(user.uid);
    if(generation!==authGeneration) return;
    const wasGuest=progress.profile==="guest",guest=wasGuest ? progress.snapshot : emptyProgress();
    const changedProfile=progress.profile!==`user:${user.uid}`;
    if(changedProfile) {progress.suspend();progress.switchProfile(`user:${user.uid}`);}
    const local=progress.snapshot;
    const merged=mergeProgress(local,remote??emptyProgress());
    // Import browser play only into an empty account. Existing account games win
    // over an unrelated guest's game on a shared computer; the guest save stays.
    for(const [game,entry] of Object.entries(guest.games)) if(!merged.games[game]) merged.games[game]=entry;
    const combined=mergeProgress(merged,emptyProgress());
    const changed=JSON.stringify(local)!==JSON.stringify(combined);
    if(changed) progress.replace(combined);
    ready=true;dirty=true;
    if(changedProfile || changed) {progress.suspend();await sync();if(generation===authGeneration) reload();}
    else {status.textContent="Progress saved to your account";void sync();}
  }
  login.onclick=()=>{
    login.disabled=true;
    void cloud.signIn().catch(error=>{
      if(error.code!=="auth/popup-closed-by-user" && error.code!=="auth/cancelled-popup-request") report(error);
    }).finally(()=>{login.disabled=false;});
  };
  logout.onclick=async()=>{
    logout.disabled=true;
    try {await sync();await cloud.signOut();}
    catch(error) {report(error);logout.disabled=false;}
  };
  if(firebaseConfig!==null) {
    void import("./game-cloud.js").then(module=>module.connectGameCloud(firebaseConfig)).then(connection=>{
      cloud=connection;
      cloud.onAuth(nextUser=>{void useAccount(nextUser).catch(report);});
    }).catch(report);
  }
  return {progress,resume};
}
