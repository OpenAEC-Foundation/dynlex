import {createFarm} from '../../web/farm-world.js';
import {createRoles} from '../../web/farm-roles.js';
import {action,itemId,targetId} from '../../web/farm-vocabulary.js';

export function savedCollectionRole() {
  const world=createFarm(),roles=createRoles(),actor=world.workers[0];
  const applied='loop forever:\n    grab fertilizer\n    put down everything\n    take 2 fertilizer from the compost heap';
  Object.assign(roles[0],{applied,source:applied+'\n# unapplied changes',generation:1,program:[
    {op:'loop',count:-1,end:5,range:null},
    {...action('collect',{argument:itemId('fertilizer'),target:targetId('barn')}),key:'take'},
    action('drop'),
    {...action('collect',{argument:itemId('fertilizer'),amount:2,target:targetId('compost')}),key:'take'},
    {op:'next',to:0,range:null}
  ]});
  roles.push({id:'unfinished',name:'New role',source:'grab ',applied:'',program:null,generation:0,needsRebuild:false});
  Object.assign(actor,{x:4,y:8,paused:true,role:roles[0].id});
  actor.routine.pc=1;actor.routine.loops={0:-1};world.compost.ready=4;
  return {version:6,games:{farm:{updatedAt:100,data:{world,roles,selected:actor.id,roleId:roles[0].id,speed:1}}},lastGame:'farm'};
}
