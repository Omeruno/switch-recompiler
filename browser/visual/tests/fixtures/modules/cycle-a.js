import {readB} from './cycle-b.js';
globalThis.moduleTrace.push('cycle-a');
export let a='A';
export function readCycle(){return a+readB();}
