import {a} from './cycle-a.js';
globalThis.moduleTrace.push('cycle-b');
export function readB(){return 'B'+a;}
