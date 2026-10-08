// 从《角色设定-全家.html》原样摘出的绘图代码（爸爸等角色），只加了最后一行导出。
const SW=16,SH=26;
const R=(c,x,y,w,h,col)=>{if(w<=0||h<=0)return;c.fillStyle=col;c.fillRect(x,y,w,h);};
const P=(c,pts,col)=>{c.fillStyle=col;pts.forEach(p=>c.fillRect(p[0],p[1],1,1));};
const lay=L=>({H0:L.kid?4:0,tH:L.kid?5:6,w:L.wide?1:0});

/* ---------- outfit helpers ---------- */
const LV=(c,y,xl,xr,tH)=>{R(c,xl,y,xr-xl+1,tH,'#f07a2a');R(c,xl,y+1,xr-xl+1,1,'#d9d9d9');P(c,[[7,y],[8,y]],'#f5f5f3');P(c,[[6,y+3],[9,y+3]],'#1e1e22');};
const LVb=(c,y,xl,xr,tH)=>{R(c,xl,y,xr-xl+1,tH,'#f07a2a');R(c,xl,y+1,xr-xl+1,1,'#d9d9d9');};
const STRIPES=(c,y,xl,xr,tH,cols)=>{for(let i=0;i<tH;i++)R(c,xl,y+i,xr-xl+1,1,cols[i%cols.length]);};

/* ---------- hair styles ---------- */
const STY={
 long:{
  back:(c,L,y,w)=>R(c,3,2+y,10,9,L.hair),
  front:(c,L,y,w)=>{R(c,5,1+y,6,1,L.hair);R(c,3,2+y,10,2,L.hair);R(c,4,4+y,8,1,L.hair);P(c,[[4,5+y],[5,5+y],[7,5+y],[8,5+y],[10,5+y],[11,5+y]],L.hair);R(c,3,4+y,1,7,L.hair);R(c,12,4+y,1,7,L.hair);R(c,11,11+y,2,6,L.hair);P(c,[[11,11+y],[12,11+y]],L.scrunch);P(c,[[6,2+y],[7,2+y]],L.hairHi);},
  up:(c,L,y,w)=>{R(c,5,1+y,6,1,L.hair);R(c,3,2+y,10,9,L.hair);R(c,7,11+y,2,7,L.hair);P(c,[[7,11+y],[8,11+y]],L.scrunch);P(c,[[6,3+y],[7,3+y]],L.hairHi);},
  side:(c,L,y)=>{R(c,5,1+y,6,1,L.hair);R(c,4,2+y,8,2,L.hair);R(c,8,4+y,4,7,L.hair);R(c,4,4+y,4,1,L.hair);P(c,[[4,5+y],[6,5+y]],L.hair);R(c,10,11+y,2,6,L.hair);P(c,[[10,11+y],[11,11+y]],L.scrunch);},
  port:(c,L)=>{R(c,9,3,14,1,L.hair);R(c,7,4,18,4,L.hair);R(c,8,8,16,2,L.hair);P(c,[[8,10],[9,10],[12,10],[13,10],[14,10],[17,10],[18,10],[21,10],[22,10],[23,10]],L.hair);R(c,6,8,3,18,L.hair);R(c,23,8,3,14,L.hair);R(c,21,22,5,10,L.hair);R(c,21,22,5,1,L.scrunch);R(c,11,5,5,1,L.hairHi);},
  portBack:(c,L)=>R(c,6,4,20,22,L.hair)
 },
 flattop:{
  front:(c,L,y,w)=>{P(c,[[5,1+y],[7,1+y],[9,1+y],[10,1+y]],L.hair);R(c,4-w,2+y,8+2*w,2,L.hair);P(c,[[4-w,4+y],[11+w,4+y]],L.fade);R(c,3-w,4+y,1,3,L.fade);R(c,12+w,4+y,1,3,L.fade);P(c,[[6,2+y],[9,2+y]],L.hairHi);},
  up:(c,L,y,w)=>{P(c,[[5,1+y],[7,1+y],[9,1+y],[10,1+y]],L.hair);R(c,4-w,2+y,8+2*w,2,L.hair);R(c,4-w,4+y,8+2*w,5,L.fade);P(c,[[3-w,7+y],[12+w,7+y]],L.skin);},
  side:(c,L,y)=>{P(c,[[5,1+y],[7,1+y],[9,1+y]],L.hair);R(c,4,2+y,7,2,L.hair);R(c,8,4+y,3,4,L.fade);P(c,[[8,6+y],[8,7+y]],L.skin);},
  port:(c,L,w)=>{P(c,[[10,3],[12,3],[14,3],[16,3],[18,3],[20,3]],L.hair);R(c,8-w,4,16+2*w,5,L.hair);R(c,8-w,9,2,5,L.fade);R(c,22+w,9,2,5,L.fade);P(c,[[11,5],[15,6],[19,5]],L.hairHi);}
 },
 sidepart:{
  front:(c,L,y,w)=>{R(c,5,1+y,6,1,L.hair);R(c,4-w,2+y,8+2*w,2,L.hair);P(c,[[4-w,4+y],[5,4+y],[6,4+y],[11+w,4+y]],L.hair);P(c,[[4-w,5+y],[11+w,5+y]],L.hair);P(c,[[9,2+y]],L.hairHi);},
  up:(c,L,y,w)=>{R(c,5,1+y,6,1,L.hair);R(c,4-w,2+y,8+2*w,7,L.hair);P(c,[[3-w,7+y],[12+w,7+y]],L.skin);},
  side:(c,L,y)=>{R(c,5,1+y,5,1,L.hair);R(c,4,2+y,7,2,L.hair);R(c,8,4+y,3,3,L.hair);P(c,[[4,4+y],[5,4+y]],L.hair);P(c,[[8,7+y]],L.skin);},
  port:(c,L,w)=>{R(c,10,3,12,1,L.hair);R(c,8-w,4,16+2*w,4,L.hair);R(c,9,8,7,2,L.hair);P(c,[[16,8]],L.hair);R(c,8-w,8,1,4,L.hair);R(c,23+w,8,1,4,L.hair);P(c,[[18,5],[19,5]],L.hairHi);}
 },
 bob:{
  back:(c,L,y,w)=>R(c,3,2+y,10,8,L.hair),
  front:(c,L,y,w)=>{R(c,5,1+y,6,1,L.hair);R(c,3,2+y,10,2,L.hair);R(c,4,4+y,8,1,L.hair);P(c,[[4,5+y],[11,5+y]],L.hair);R(c,3,4+y,1,6,L.hair);R(c,12,4+y,1,6,L.hair);P(c,[[6,2+y]],L.hairHi);},
  up:(c,L,y,w)=>{R(c,5,1+y,6,1,L.hair);R(c,3,2+y,10,8,L.hair);},
  side:(c,L,y)=>{R(c,5,1+y,6,1,L.hair);R(c,4,2+y,8,2,L.hair);R(c,8,4+y,4,6,L.hair);R(c,4,4+y,3,1,L.hair);},
  port:(c,L)=>{R(c,9,3,14,1,L.hair);R(c,7,4,18,4,L.hair);R(c,8,8,16,2,L.hair);R(c,7,8,3,14,L.hair);R(c,22,8,3,14,L.hair);R(c,11,5,4,1,L.hairHi);}
 },
 tied:{
  front:(c,L,y,w)=>{R(c,5,1+y,6,1,L.hair);R(c,4,2+y,8,2,L.hair);P(c,[[4,4+y],[5,4+y],[11,4+y],[4,5+y],[11,5+y]],L.hair);P(c,[[7,2+y]],L.hairHi);},
  up:(c,L,y,w)=>{R(c,5,1+y,6,1,L.hair);R(c,4,2+y,8,6,L.hair);R(c,7,8+y,2,5,L.hair);},
  side:(c,L,y)=>{R(c,5,1+y,6,1,L.hair);R(c,4,2+y,8,2,L.hair);R(c,8,4+y,3,3,L.hair);R(c,11,4+y,1,5,L.hair);},
  port:(c,L)=>{R(c,10,3,12,1,L.hair);R(c,8,4,16,4,L.hair);R(c,8,8,3,5,L.hair);R(c,21,8,3,5,L.hair);R(c,24,9,2,10,L.hair);P(c,[[13,5],[14,5]],L.hairHi);}
 },
 kid:{
  front:(c,L,y,w)=>{R(c,5,1+y,6,1,L.hair);R(c,4,2+y,8,2,L.hair);R(c,4,4+y,8,1,L.hair);P(c,[[4,5+y],[6,5+y],[9,5+y],[11,5+y]],L.hair);},
  up:(c,L,y,w)=>{R(c,5,1+y,6,1,L.hair);R(c,4,2+y,8,7,L.hair);P(c,[[3,7+y],[12,7+y]],L.skin);},
  side:(c,L,y)=>{R(c,5,1+y,6,1,L.hair);R(c,4,2+y,7,2,L.hair);R(c,4,4+y,4,1,L.hair);R(c,8,4+y,3,3,L.hair);P(c,[[8,7+y]],L.skin);},
  port:(c,L)=>{R(c,10,3,12,1,L.hair);R(c,8,4,16,4,L.hair);R(c,9,8,14,2,L.hair);P(c,[[9,10],[10,10],[14,10],[15,10],[20,10],[21,10]],L.hair);R(c,8,8,1,4,L.hair);R(c,23,8,1,4,L.hair);}
 },
 teen:{
  front:(c,L,y,w)=>{P(c,[[5,1+y],[7,1+y],[10,1+y]],L.hair);R(c,3,2+y,10,2,L.hair);R(c,4,4+y,8,1,L.hair);P(c,[[4,5+y],[6,5+y],[7,5+y],[10,5+y],[11,5+y]],L.hair);R(c,3,4+y,1,4,L.hair);R(c,12,4+y,1,4,L.hair);},
  up:(c,L,y,w)=>{P(c,[[5,1+y],[7,1+y],[10,1+y]],L.hair);R(c,3,2+y,10,8,L.hair);},
  side:(c,L,y)=>{P(c,[[5,1+y],[8,1+y]],L.hair);R(c,4,2+y,8,2,L.hair);R(c,4,4+y,4,1,L.hair);P(c,[[4,5+y]],L.hair);R(c,8,4+y,4,5,L.hair);},
  port:(c,L)=>{P(c,[[10,2],[13,2],[17,2],[20,2]],L.hair);R(c,9,3,14,1,L.hair);R(c,7,4,18,4,L.hair);R(c,8,8,16,3,L.hair);P(c,[[8,11],[11,11],[12,11],[16,11],[20,11],[21,11]],L.hair);R(c,7,8,2,8,L.hair);R(c,23,8,2,8,L.hair);}
 }
};

/* ---------- body ---------- */
function bodyFB(c,L,O,d,f){
  const {H0,tH,w}=lay(L);const l1=f===1?1:0,l2=f===2?1:0;
  const yT=12+H0,yL=yT+tH,xl=4-w,xr=11+w;
  if(O.lower==='pants'){
    const lw=3+w,lx=xl+1;
    R(c,lx,yL,lw,24-yL-l1,O.pants);R(c,8,yL,lw,24-yL-l2,O.pants);
    R(c,8,yL+1,1,22-yL-l2,O.pantsDark||O.pants);
    R(c,lx,24-l1,lw,2,O.shoes);R(c,8,24-l2,lw,2,O.shoes);
  }else{
    const bh=O.lower==='shorts'?2:3,lt=yL+bh,lc=O.socks||L.skin;
    R(c,5,lt,2,24-lt-l1,lc);R(c,9,lt,2,24-lt-l2,lc);
    R(c,5,24-l1,2,2,O.shoes);R(c,9,24-l2,2,2,O.shoes);
    if(O.lower==='shorts'){R(c,xl+1,yL,3+w,2,O.pants);R(c,8,yL,3+w,2,O.pants);}
    else{const col=O.lower==='dress'?O.top:O.skirt;R(c,xl,yL,8+2*w,bh-1,col);R(c,xl-1,yL+bh-1,10+2*w,1,col);
      if(O.skirtLine)P(c,[[5,yL],[5,yL+1],[8,yL],[8,yL+1],[11,yL+1],[4,yL+2],[7,yL+2],[10,yL+2]],O.skirtLine);
      if(O.lower==='dress'&&O.pat)P(c,[[5,yL],[9,yL+1],[7,yL+2],[11,yL],[4,yL+2],[10,yL+2]],O.pat);}
  }
  const b=L.belly?1:0,bl=xl-b,br=xr+b;
  R(c,xl,yT,8+2*w,1,O.top);R(c,bl,yT+1,br-bl+1,tH-1+(O.coat?3:0),O.top);
  const al=bl-1,ar=br+1,sl=O.short?2:tH;
  R(c,al,yT+1+l2,1,sl,O.sleeve);R(c,ar,yT+1+l1,1,sl,O.sleeve);
  if(b){R(c,al+1,yT,1,1,O.sleeve);R(c,ar-1,yT,1,1,O.sleeve);}
  if(O.short){R(c,al,yT+3+l2,1,tH-2,L.skin);R(c,ar,yT+3+l1,1,tH-2,L.skin);}
  if(O.armStripe){P(c,[[al,yT+3+l2],[al,yT+5+l2],[ar,yT+3+l1],[ar,yT+5+l1]],O.armStripe);}
  R(c,al,yT+tH+1+l2,1,1,L.skin);R(c,ar,yT+tH+1+l1,1,1,L.skin);
  if(d==='down'){O.fr&&O.fr(c,yT,bl,br,tH);}else{O.bk&&O.bk(c,yT,bl,br,tH);}
  if(b){R(c,bl+1,yL,br-bl-1,1,d==='down'&&O.fr&&O.bellyCol?O.bellyCol:O.top);R(c,bl+2,yL+1,br-bl-3,1,'rgba(0,0,0,0.18)');P(c,[[bl,yT+tH-1],[br,yT+tH-1]],'rgba(0,0,0,0.15)');}
  R(c,7,yT-1,2,1,L.skin);
}
function features(c,L,O,y,w){
  if(L.blush)P(c,[[5-w,9+y],[10+w,9+y]],L.blush);
  const sun=O.sun||L.sun;
  if(sun){R(c,5,7+y,6,1,sun);P(c,[[5,8+y],[6,8+y],[9,8+y],[10,8+y]],sun);}
  else{
    if(L.tallEye){R(c,6,7+y,1,2,L.eye);R(c,9,7+y,1,2,L.eye);}else P(c,[[6,7+y],[9,7+y]],L.eye);
    if(L.glasses){const g='#5c606c';R(c,5,6+y,3,1,g);R(c,8,6+y,3,1,g);P(c,[[5,7+y],[7,7+y],[8,7+y],[10,7+y]],g);if(L.brow)P(c,[[5,5+y],[6,5+y],[9,5+y],[10,5+y]],L.brow);}
    else if(L.brow)P(c,[[5,6+y],[6,6+y],[9,6+y],[10,6+y]],L.brow);
  }
  if(L.must)R(c,6,9+y,4,1,L.must);
  else if(L.grin){R(c,6,9+y,4,1,L.mouth);P(c,[[7,9+y],[8,9+y]],'#ffffff');}
  else P(c,[[7,9+y],[8,9+y]],L.mouth);
}
function helmetF(c,O,y,w){R(c,3-w,1+y,10+2*w,4,O.helmet);R(c,4-w,1+y,4,1,'#f27a7a');P(c,[[4-w,9+y],[11+w,9+y]],'#2a2a2a');}
function headDown(c,L,O){
  const {H0,w}=lay(L),y=H0,st=STY[L.hs];
  if(st.back&&!O.helmet)st.back(c,L,y,w);
  R(c,4-w,4+y,8+2*w,7,L.skin);
  if(L.ears)P(c,[[3-w,7+y],[12+w,7+y]],L.skin);
  features(c,L,O,y,w);
  if(O.helmet)helmetF(c,O,y,w);else st.front(c,L,y,w);
}
function headUp(c,L,O){
  const {H0,w}=lay(L),y=H0,st=STY[L.hs];
  R(c,4-w,4+y,8+2*w,7,L.skin);
  st.up(c,L,y,w);
  if(O.helmet){R(c,3-w,1+y,10+2*w,5,O.helmet);R(c,5,1+y,4,1,'#f27a7a');}
}
/* ---------- side ---------- */
function side(c,L,O,f){
  const {H0,tH,w}=lay(L),y=H0,yT=12+H0,yL=yT+tH;
  const xs=f===0?[6]:f===1?[4,9]:[5,8];
  if(O.lower==='pants'){xs.forEach(x=>{R(c,x,yL,2,24-yL,O.pants);R(c,x-1,24,3,2,O.shoes);});}
  else{const bh=O.lower==='shorts'?2:3,lc=O.socks||L.skin;
    xs.forEach(x=>{R(c,x,yL+bh,2,24-yL-bh,lc);R(c,x-1,24,3,2,O.shoes);});
    if(O.lower==='shorts')xs.forEach(x=>R(c,x,yL,2,2,O.pants));
    else{const col=O.lower==='dress'?O.top:O.skirt;R(c,5,yL,6,bh-1,col);R(c,4,yL+bh-1,8,1,col);if(O.skirtLine)P(c,[[6,yL],[9,yL+1],[5,yL+2],[8,yL+2]],O.skirtLine);}
  }
  R(c,5,yT,6+w,tH+(O.coat?3:0),O.top);
  O.sd&&O.sd(c,yT,tH);
  if(L.belly){const bc=O.bellyCol||O.top;R(c,4,yT+1,1,tH,bc);R(c,3,yT+2,1,tH-2,bc);R(c,2,yT+3,1,tH-4,bc);R(c,5,yT+tH,5,1,bc);P(c,[[3,yT+tH-1],[4,yT+tH]],'rgba(0,0,0,0.18)');}
  const ax=f===1?5:f===2?8:7;
  if(O.short){R(c,ax,yT,2,2,O.sleeve);R(c,ax,yT+2,2,tH-2,L.skin);}else R(c,ax,yT,2,tH,O.sleeve);
  if(O.armStripe){R(c,ax,yT+2,2,1,O.armStripe);R(c,ax,yT+4,2,1,O.armStripe);}
  R(c,ax,yT+tH,2,1,L.skin);
  R(c,7,yT-1,2,1,L.skin);
  R(c,4,4+y,6+w,7,L.skin);P(c,[[3,7+y]],L.skin);
  const sun=O.sun||L.sun;
  if(sun){R(c,4,7+y,3,1,sun);P(c,[[4,8+y]],sun);}
  else{if(L.tallEye)R(c,5,7+y,1,2,L.eye);else P(c,[[5,7+y]],L.eye);
    if(L.glasses){R(c,4,6+y,3,1,'#5c606c');P(c,[[4,7+y],[6,7+y],[7,7+y]],'#5c606c');if(L.brow)P(c,[[4,5+y],[5,5+y]],L.brow);}
    else if(L.brow)P(c,[[4,6+y],[5,6+y]],L.brow);}
  if(L.blush)P(c,[[6,9+y]],L.blush);
  if(L.must)P(c,[[3,9+y],[4,9+y]],L.must);else P(c,[[4,9+y]],L.mouth);
  if(O.helmet){R(c,3,1+y,9,4,O.helmet);R(c,4,1+y,3,1,'#f27a7a');P(c,[[7,8+y]],'#2a2a2a');}
  else STY[L.hs].side(c,L,y);
}
function drawChar(c,L,O,d,f){
  c.clearRect(0,0,SW,SH);
  if(d==='right'){c.save();c.translate(SW,0);c.scale(-1,1);side(c,L,O,f);c.restore();return;}
  if(d==='left'){side(c,L,O,f);return;}
  bodyFB(c,L,O,d,f);
  if(d==='down')headDown(c,L,O);else headUp(c,L,O);
}
/* ---------- portrait ---------- */
function shadeOf(hex,k){const n=parseInt(hex.slice(1),16);const r=Math.max(0,(n>>16)-k),g=Math.max(0,((n>>8)&255)-k-6),b=Math.max(0,(n&255)-k-6);return '#'+((1<<24)|(r<<16)|(g<<8)|b).toString(16).slice(1);}
function portrait(c,L,O,ex){
  c.clearRect(0,0,32,32);R(c,0,0,32,32,L.pbg);
  const st=STY[L.hs],w=L.wide?1:0,E=L.eye;
  if(st.portBack&&!O.helmet)st.portBack(c,L);
  R(c,4,27,24,5,O.top);if(O.pd)O.pd(c);
  R(c,13,23,6,4,L.skin);
  R(c,9-w,8,14+2*w,14,L.skin);R(c,10-w,22,12+2*w,1,L.skin);R(c,12,23,8,1,L.skin);
  if(L.ears){R(c,7-w,13,2,4,L.skin);R(c,23+w,13,2,4,L.skin);}
  if(O.helmet){R(c,7-w,2,18+2*w,8,O.helmet);R(c,10,3,6,1,'#f27a7a');R(c,8-w,10,1,10,'#2a2a2a');R(c,23+w,10,1,10,'#2a2a2a');}
  else st.port(c,L,w);
  if(L.blush)P(c,[[10,18],[11,18],[20,18],[21,18]],L.blush);
  P(c,[[16,17]],shadeOf(L.skin,22));
  const sun=O.sun||L.sun;
  const browC=L.brow||L.hair;
  if(L.brow&&ex!=='sad'&&!sun){R(c,10,L.glasses?11:12,4,1,browC);R(c,18,L.glasses?11:12,4,1,browC);}
  if(sun){R(c,10,13,5,4,sun);R(c,17,13,5,4,sun);R(c,15,14,2,1,sun);P(c,[[11,14],[18,14]],'#8a8a96');}
  else if(ex==='smile'){P(c,[[11,15],[12,14],[13,14],[14,15],[18,15],[19,14],[20,14],[21,15]],E);}
  else if(ex==='sad'){R(c,11,15,3,2,E);R(c,18,15,3,2,E);P(c,[[10,13],[11,13],[12,12],[19,12],[20,13],[21,13]],browC);}
  else if(ex==='surprised'){R(c,11,13,3,4,E);R(c,18,13,3,4,E);P(c,[[11,13],[18,13]],'#ffffff');}
  else{if(L.tallEye){R(c,11,14,3,3,E);R(c,18,14,3,3,E);P(c,[[11,14],[18,14]],'#ffffff');}else{R(c,11,15,3,2,E);R(c,18,15,3,2,E);}}
  if(L.glasses&&!sun){const g=L.glasses;R(c,10,12,5,1,g);R(c,17,12,5,1,g);R(c,10,17,5,1,g);R(c,17,17,5,1,g);R(c,10,12,1,6,g);R(c,14,12,1,6,g);R(c,17,12,1,6,g);R(c,21,12,1,6,g);R(c,15,13,2,1,g);}
  if(L.must)R(c,12,18,8,1,L.must);
  if(ex==='smile'){if(L.grin){R(c,13,19,6,2,'#8a3a3a');R(c,14,19,4,1,'#ffffff');}else P(c,[[14,19],[15,20],[16,20],[17,19]],L.mouth);}
  else if(ex==='sad')P(c,[[14,20],[15,19],[16,19],[17,20]],L.mouth);
  else if(ex==='surprised')R(c,15,19,2,2,'#8a3f3f');
  else{if(L.grin){R(c,13,19,6,1,L.mouth);R(c,14,19,4,1,'#ffffff');}else R(c,14,19,4,1,L.mouth);}
}

/* ---------- cast ---------- */
const TEE='#f5f5f3',RUB='#4f5a3a';
const CAST=[
 {key:'me',name:'我',tag:'现在',skin:'#eac2a0',hair:'#141418',hairHi:'#2e2e36',fade:'#3e3f47',eye:'#1e1a1a',brow:'#1a1618',mouth:'#b27a6e',hs:'flattop',wide:1,ears:true,glasses:'#1b1b20',pbg:'#d8e1ea',
  note:'寸头、两边推短，脸圆一点，黑框眼镜。平时那件白 T，加上黑色卫衣、漂流装，白大褂留作上班场景。',
  outfits:{
   tee:{label:'白 T',top:TEE,sleeve:TEE,short:true,lower:'pants',pants:'#2f3440',pantsDark:'#262a34',shoes:'#e6e2da',
     fr:(c,y)=>{R(c,6,y+1,4,3,'#8a8d96');P(c,[[7,y+2],[8,y+1],[9,y+2]],'#c8323a');},sd:(c,y)=>{R(c,5,y+1,2,3,'#8a8d96');},
     pd:c=>{R(c,12,28,8,4,'#8a8d96');P(c,[[14,29],[15,28],[16,30]],'#c8323a');}},
   hoodie:{label:'黑卫衣',top:'#1d1d22',sleeve:'#1d1d22',lower:'pants',pants:'#6b81a3',pantsDark:'#5a6f90',shoes:'#e6e2da',
     fr:(c,y)=>{P(c,[[5,y],[10,y]],'#2e2e35');P(c,[[7,y+1],[8,y+2]],'#d8d8d8');R(c,5,y+3,6,2,'#e8e8e8');P(c,[[6,y+3],[8,y+4],[9,y+3]],'#1d1d22');},
     bk:(c,y)=>R(c,5,y,6,2,'#2a2a30'),sd:(c,y)=>R(c,9,y,3,2,'#2a2a30'),
     pd:c=>{R(c,10,27,12,1,'#2e2e35');R(c,10,29,12,3,'#e8e8e8');P(c,[[13,28],[18,28]],'#d8d8d8');}},
   raft:{label:'漂流',top:TEE,sleeve:TEE,short:true,helmet:'#d8323a',lower:'shorts',pants:'#1f1f24',shoes:RUB,
     fr:(c,y,xl,xr,t)=>LV(c,y,xl,xr,t),bk:(c,y,xl,xr,t)=>LVb(c,y,xl,xr,t),sd:(c,y,t)=>R(c,5,y,7,t,'#f07a2a'),
     pd:c=>{R(c,6,27,20,5,'#f07a2a');R(c,6,28,20,1,'#d9d9d9');R(c,13,27,6,1,TEE);}},
   coat:{label:'白大褂',top:'#f3f5f7',sleeve:'#f3f5f7',coat:true,lower:'pants',pants:'#2b2f3a',pantsDark:'#22252e',shoes:'#16161a',
     fr:(c,y)=>{R(c,6,y,4,2,'#ffffff');R(c,7,y,2,4,'#3d5a8a');P(c,[[5,y],[10,y],[6,y+1],[9,y+1]],'#cfd6de');R(c,7,y+4,1,6,'#cfd6de');},
     bk:(c,y)=>R(c,7,y+5,2,5,'#cfd6de'),sd:(c,y)=>R(c,5,y+4,1,6,'#cfd6de'),
     pd:c=>{R(c,12,27,8,5,'#ffffff');R(c,15,27,2,5,'#3d5a8a');}}
  }},
 {key:'teen',name:'高中的我',tag:'高中',skin:'#ecc6a4',hair:'#141418',eye:'#1e1a1a',brow:'#1a1618',mouth:'#b27a6e',hs:'teen',ears:false,pbg:'#dfe3d6',
  note:'高中那张：头发长、刘海盖住额头，还没戴眼镜，黑色卫衣里面露出灰 T。',
  outfits:{hoodie:{label:'黑卫衣',top:'#1f1f25',sleeve:'#1f1f25',lower:'pants',pants:'#3a4152',shoes:'#2a2a30',
    fr:(c,y)=>{R(c,7,y,2,3,'#6e7079');P(c,[[5,y+3],[6,y+3],[9,y+3],[10,y+4],[5,y+5]],'#d8d8d8');},
    bk:(c,y)=>R(c,5,y,6,2,'#2a2a30'),sd:(c,y)=>R(c,5,y,1,3,'#6e7079'),
    pd:c=>{R(c,13,27,6,5,'#6e7079');P(c,[[7,30],[9,30],[22,30],[24,30]],'#d8d8d8');}}}},
 {key:'kid',name:'小时候的我',tag:'小学',kid:true,skin:'#efcaa8',hair:'#18181c',eye:'#1e1a1a',mouth:'#c0786c',hs:'kid',blush:'#f0b2a6',tallEye:true,ears:true,pbg:'#f1e3c8',
  note:'小时候那张：白 T 恤、深色领口，头身比做成大头小身子，站在一起比大人矮一截。',
  outfits:{tee:{label:'白 T',top:'#f3f3f0',sleeve:'#f3f3f0',short:true,lower:'shorts',pants:'#3d5a8a',shoes:'#f0f0f0',
    fr:(c,y)=>R(c,6,y,4,1,'#5a5a6e'),pd:c=>R(c,11,27,10,1,'#5a5a6e')}}},
 {key:'dad',name:'爸爸',tag:'现在',skin:'#e2b48e',hair:'#16161a',hairHi:'#303038',eye:'#1e1a1a',brow:'#1a1618',mouth:'#a86e60',hs:'sidepart',wide:1,belly:true,grin:true,ears:true,pbg:'#dbe4d4',
  note:'侧分短发，圆脸，笑得开，肚子圆出来。灰格子夹克配花纹毛衣是招牌，还有一件绿蓝条纹 polo 和漂流装。',
  outfits:{
   jacket:{label:'格子夹克',top:'#4a4f66',bellyCol:'#4a4f66',sleeve:'#3d4048',lower:'pants',pants:'#2b2e36',pantsDark:'#23262d',shoes:'#3a2a22',
     fr:(c,y,xl,xr,t)=>{R(c,xl,y,2,t,'#3d4048');R(c,xr-1,y,2,t,'#3d4048');P(c,[[6,y+1],[8,y+2],[7,y+4],[9,y+3]],'#8c4a6a');P(c,[[7,y+1],[9,y+1],[6,y+3],[8,y+4]],'#5f7ab0');P(c,[[6,y],[9,y]],'#6a5a7a');P(c,[[xl,y+2],[xr,y+4]],'#5a5e68');},
     bk:(c,y,xl,xr,t)=>{R(c,xl,y,xr-xl+1,t,'#3d4048');R(c,xl,y+2,xr-xl+1,1,'#4a4e58');},sd:(c,y,t)=>{R(c,6,y,6,t,'#3d4048');P(c,[[5,y+2],[5,y+4]],'#8c4a6a');},
     pd:c=>{R(c,4,27,7,5,'#3d4048');R(c,21,27,7,5,'#3d4048');P(c,[[12,29],[15,30],[18,29],[13,31],[17,31]],'#8c4a6a');P(c,[[14,28],[16,28],[19,30]],'#5f7ab0');}},
   polo:{label:'条纹 polo',top:'#1f4f4a',sleeve:'#1f4f4a',lower:'pants',pants:'#2b2e36',shoes:'#2a2420',
     fr:(c,y,xl,xr,t)=>{STRIPES(c,y,xl,xr,t,['#1f4f4a','#1f4f4a','#2c3150']);P(c,[[6,y],[9,y]],'#16302d');P(c,[[7,y+1]],'#16302d');},
     bk:(c,y,xl,xr,t)=>STRIPES(c,y,xl,xr,t,['#1f4f4a','#1f4f4a','#2c3150']),sd:(c,y,t)=>STRIPES(c,y,5,11,t,['#1f4f4a','#1f4f4a','#2c3150']),
     pd:c=>{R(c,4,29,24,1,'#2c3150');R(c,13,27,6,1,'#16302d');}},
   raft:{label:'漂流',top:'#e8e8e8',sleeve:'#e8e8e8',short:true,helmet:'#d8323a',bellyCol:'#f07a2a',lower:'pants',pants:'#3b4f72',shoes:RUB,armStripe:'#7a7a86',
     fr:(c,y,xl,xr,t)=>LV(c,y,xl,xr,t),bk:(c,y,xl,xr,t)=>LVb(c,y,xl,xr,t),sd:(c,y,t)=>R(c,5,y,7,t,'#f07a2a'),
     pd:c=>{R(c,6,27,20,5,'#f07a2a');R(c,6,28,20,1,'#d9d9d9');}}
  }},
 {key:'dadY',name:'年轻时的爸爸',tag:'年轻时',skin:'#e8bf9a',hair:'#16161a',hairHi:'#303038',eye:'#1e1a1a',brow:'#1a1618',mouth:'#a86e60',must:'#3a2c26',hs:'sidepart',ears:true,pbg:'#d9dde8',
  note:'老照片里那身：酒红毛衣、白衬衫领子、深色领带，嘴上一撇小胡子。',
  outfits:{sweater:{label:'酒红毛衣',top:'#7a1e2a',sleeve:'#7a1e2a',lower:'pants',pants:'#2b2e36',shoes:'#1e1a18',
    fr:(c,y)=>{P(c,[[6,y],[9,y],[7,y],[8,y]],'#f2f2f2');R(c,7,y+1,2,3,'#1e2433');},sd:(c,y)=>P(c,[[5,y],[5,y+1]],'#f2f2f2'),
    pd:c=>{R(c,12,27,8,2,'#f2f2f2');R(c,15,27,2,5,'#1e2433');}}}},
 {key:'cousin',name:'表哥',tag:'',skin:'#e6bc98',hair:'#121214',eye:'#1e1a1a',brow:'#1a1618',mouth:'#ad7466',hs:'sidepart',grin:true,ears:true,pbg:'#e2dccd',
  note:'短发，笑起来很开。黑色绗缝皮夹克里面是蓝格子衬衫，配牛仔裤；另一套是蓝衬衫。',
  outfits:{
   leather:{label:'皮夹克',top:'#5a6f9a',sleeve:'#1a1a1f',lower:'pants',pants:'#3d5582',pantsDark:'#33486e',shoes:'#2a2a2e',
     fr:(c,y,xl,xr,t)=>{R(c,xl,y,2,t,'#1a1a1f');R(c,xr-1,y,2,t,'#1a1a1f');P(c,[[6,y+1],[8,y+1],[7,y+3],[9,y+3],[6,y+5]],'#c9cfd8');P(c,[[xl,y+2],[xr,y+3]],'#2c2c33');},
     bk:(c,y,xl,xr,t)=>{R(c,xl,y,xr-xl+1,t,'#1a1a1f');P(c,[[6,y+2],[9,y+2],[7,y+4]],'#2c2c33');},sd:(c,y,t)=>R(c,6,y,6,t,'#1a1a1f'),
     pd:c=>{R(c,4,27,8,5,'#1a1a1f');R(c,20,27,8,5,'#1a1a1f');P(c,[[13,28],[16,29],[18,28],[14,31]],'#c9cfd8');}},
   shirt:{label:'蓝衬衫',top:'#7f9fd8',sleeve:'#7f9fd8',short:true,lower:'pants',pants:'#2b2e36',shoes:'#2a2a2e',
     fr:(c,y)=>{P(c,[[6,y],[9,y]],'#6a88c0');R(c,7,y+1,1,4,'#6a88c0');},pd:c=>{R(c,12,27,3,2,'#6a88c0');R(c,17,27,3,2,'#6a88c0');}}
  }},
 {key:'her',name:'她',tag:'摄影模特朋友',skin:'#f6dccb',blush:'#efb2a6',hair:'#16151b',hairHi:'#35333e',scrunch:'#ece4d4',eye:'#231c22',mouth:'#c4706c',hs:'long',tallEye:true,pbg:'#e9dfd0',
  note:'黑长直、碎刘海、低马尾搭在右肩（白发圈）。四套衣服对应你拍的四组照片。',
  outfits:{
   jk:{label:'灰马甲',top:'#f5f3ee',sleeve:'#f5f3ee',lower:'skirt',skirt:'#2f3a5c',skirtLine:'#56658f',socks:'#f4f4f4',shoes:'#1e1e22',
     fr:(c,y)=>{R(c,5,y+1,6,5,'#a8adb4');P(c,[[6,y+1],[9,y+1]],'#3c7d8a');P(c,[[7,y+1],[8,y+1],[7,y+2],[8,y+3]],'#4f9aa8');},bk:(c,y)=>R(c,5,y+1,6,5,'#a8adb4'),sd:(c,y)=>{R(c,6,y+1,4,5,'#a8adb4');P(c,[[5,y+1]],'#4f9aa8');},
     pd:c=>{R(c,8,28,16,4,'#a8adb4');R(c,13,28,6,2,'#4f9aa8');P(c,[[12,28],[19,28]],'#3c7d8a');}},
   cardigan:{label:'藏青开衫',top:'#232a44',sleeve:'#232a44',lower:'skirt',skirt:'#5e3440',skirtLine:'#9aa0a8',socks:'#f4f4f4',shoes:'#1e1e22',
     fr:(c,y)=>{R(c,6,y,4,1,'#f5f3ee');P(c,[[7,y+1],[8,y+1],[7,y+2],[8,y+3]],'#a3283a');P(c,[[7,y+4],[7,y+5]],'#d6b25e');},sd:(c,y)=>{P(c,[[5,y]],'#f5f3ee');P(c,[[5,y+1],[5,y+2]],'#a3283a');},
     pd:c=>{R(c,12,27,8,1,'#f5f3ee');R(c,15,28,2,4,'#a3283a');}},
   sailor:{label:'水手服',top:'#f6f6f4',sleeve:'#f6f6f4',lower:'skirt',skirt:'#1f2a4a',skirtLine:'#2e3c63',socks:'#f7f7f7',shoes:'#1e1e22',
     fr:(c,y)=>{R(c,4,y,8,1,'#2c4f8a');P(c,[[5,y+1],[6,y+1],[9,y+1],[10,y+1],[6,y+2],[9,y+2]],'#2c4f8a');P(c,[[7,y+1],[8,y+1],[7,y+2],[8,y+2],[7,y+3]],'#c8323a');},
     bk:(c,y)=>{R(c,4,y,8,3,'#2c4f8a');R(c,4,y+2,8,1,'#e9eef5');},sd:(c,y)=>{R(c,8,y,3,3,'#2c4f8a');P(c,[[5,y+1],[5,y+2]],'#c8323a');},
     pd:c=>{R(c,4,27,24,2,'#2c4f8a');R(c,14,28,4,3,'#c8323a');}},
   casual:{label:'条纹毛衣',top:'#eee7da',sleeve:'#eee7da',armStripe:'#2b2b30',lower:'pants',pants:'#93a9c6',pantsDark:'#7d93b0',shoes:'#e8e4dc',
     fr:(c,y)=>{P(c,[[7,y],[8,y]],'#d9d2c4');R(c,4,y+3,8,1,'#2b2b30');},bk:(c,y)=>R(c,4,y+3,8,1,'#2b2b30'),sd:(c,y)=>R(c,5,y+3,6,1,'#2b2b30'),
     pd:c=>R(c,4,30,24,1,'#2b2b30')}
  }}
];


export { SW, SH, R, P, drawChar, portrait, CAST, STY, shadeOf };
