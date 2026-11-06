import {runs,increment} from './module-state.js';
export async function run(){await Promise.resolve();increment();return runs;}
