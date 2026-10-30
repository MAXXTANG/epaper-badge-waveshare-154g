import assert from 'node:assert/strict';
import {SIZE,COLORS,layout,quantize} from '../web/dist/image.mjs';
const full=layout(240,240,'fit');assert.equal(full.scale,200/240);assert.equal(full.panX,0);assert.equal(full.panY,0);
const portrait=layout(200,400,'fit');assert.equal(portrait.scale,.5);
const fill=layout(200,400,'fill',1,0,500,500);assert.equal(fill.scale,1);assert.equal(fill.panX,0);assert.equal(fill.panY,100);
const rotate=layout(200,400,'fill',1,90);assert.equal(rotate.scale,1);
const zoom=layout(240,240,'fill',1.2,0,100,-100);assert.equal(zoom.panX,20);assert.equal(zoom.panY,-20);
for(let code=0;code<4;code++)for(const dither of [false,true]){
  const data=new Uint8ClampedArray(SIZE*SIZE*4);
  for(let i=0;i<SIZE*SIZE;i++){data.set(COLORS[code],i*4);data[i*4+3]=255;}
  const {packed,preview}=quantize(data,dither);assert.equal(packed.length,10000);
  for(const byte of packed)assert.equal(byte,code*0x55);
  assert.deepEqual([...preview.subarray(0,3)],COLORS[code]);
}
assert.throws(()=>quantize(new Uint8Array(40)),/200/);
console.log('PASS: full-square bounds, portrait fit/fill, rotation, zoom clamps, all 4 panel codes with/without dithering, 10000-byte upload format');
