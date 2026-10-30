export const SIZE = 200;
export const COLORS = [[0,0,0],[255,255,255],[255,230,0],[225,25,25]];
export function layout(width,height,mode,zoom=1,rotation=0,panX=0,panY=0) {
  const swapped = Math.abs(rotation%180)===90;
  const w = swapped ? height : width, h = swapped ? width : height;
  const scale = (mode==='fill' ? Math.max(SIZE/w,SIZE/h) : Math.min(SIZE/w,SIZE/h))*zoom;
  const maxX=Math.max(0,(w*scale-SIZE)/2),maxY=Math.max(0,(h*scale-SIZE)/2);
  return {scale,panX:Math.max(-maxX,Math.min(maxX,panX)),panY:Math.max(-maxY,Math.min(maxY,panY))};
}
export function nearest(r,g,b) {
  let best=Infinity,code=0;
  COLORS.forEach((c,i)=>{
    const d=.30*(r-c[0])**2+.59*(g-c[1])**2+.11*(b-c[2])**2;
    if(d<best){best=d;code=i;}
  });
  return code;
}
export function toneChannel(value,brightness){return Math.max(0,Math.min(255,value+brightness*2.55));}
export function rgbaToRgb(rgba){
  if(rgba.length!==SIZE*SIZE*4)throw new Error('Image must be 200 × 200');
  const rgb=new Uint8Array(SIZE*SIZE*3);for(let p=0;p<SIZE*SIZE;p++)rgb.set(rgba.subarray(p*4,p*4+3),p*3);return rgb;
}
export function rgbToRgba(rgb){
  if(rgb.length!==SIZE*SIZE*3)throw new Error('Invalid RGB image');
  const rgba=new Uint8ClampedArray(SIZE*SIZE*4);for(let p=0;p<SIZE*SIZE;p++){rgba.set(rgb.subarray(p*3,p*3+3),p*4);rgba[p*4+3]=255;}return rgba;
}
export function quantize(rgba,dither=true,brightness=0) {
  if(rgba.length!==SIZE*SIZE*4) throw new Error('Image must be 200 × 200');
  const packed=new Uint8Array(SIZE*SIZE/4),preview=new Uint8ClampedArray(rgba.length);
  const error=[new Float32Array((SIZE+2)*3),new Float32Array((SIZE+2)*3)];
  for(let y=0;y<SIZE;y++) {
    const cur=error[y%2],next=error[(y+1)%2];next.fill(0);
    const dir=y%2?-1:1;
    for(let xi=0;xi<SIZE;xi++) {
      const x=dir===1?xi:SIZE-1-xi,pos=y*SIZE+x,at=pos*4;
      const value=[0,1,2].map(c=>Math.max(0,Math.min(255,toneChannel(rgba[at+c],brightness)+(dither?cur[(x+1)*3+c]:0))));
      const code=nearest(...value),color=COLORS[code];
      packed[pos>>2]|=code<<((3-pos%4)*2);
      for(let c=0;c<3;c++) {
        preview[at+c]=color[c];
        if(dither) {
          const e=value[c]-color[c];
          cur[(x+1+dir)*3+c]+=e*7/16;
          next[(x+1-dir)*3+c]+=e*3/16;
          next[(x+1)*3+c]+=e*5/16;
          next[(x+1+dir)*3+c]+=e/16;
        }
      }
      preview[at+3]=255;
    }
  }
  return {packed,preview};
}
