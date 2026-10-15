const fs = require('fs');
const path = require('path');

// 输入: 本文件夹里的 gbk_compressed.hex; 输出: ../firmware/html_page.h
const HEX_PATH = path.join(__dirname, 'gbk_compressed.hex');
const OUT_PATH = path.join(__dirname, '..', 'firmware', 'html_page.h');

// Read the compressed GBK table
const compressedHex = fs.readFileSync(HEX_PATH, 'utf8').trim();
console.log('Compressed table size:', compressedHex.length, 'chars (' + (compressedHex.length / 1024).toFixed(1) + ' KB)');
console.log('Number of ranges:', compressedHex.length / 10);

// Build the JS block with GBK table
var jsBlock = '';
jsBlock += '// ===== GBK compressed table =====\n';
jsBlock += '// Format: each range = 10 hex chars = startUni(4) + len(2) + startGbk(4)\n';
jsBlock += 'var GBK_RANGES_HEX=\'' + compressedHex + '\';\n';
jsBlock += 'var GR=[];\n';
jsBlock += 'for(var _i=0;_i<GBK_RANGES_HEX.length;_i+=10){\n';
jsBlock += '  GR.push({\n';
jsBlock += '    u:parseInt(GBK_RANGES_HEX.substr(_i,4),16),\n';
jsBlock += '    n:parseInt(GBK_RANGES_HEX.substr(_i+4,2),16),\n';
jsBlock += '    g:parseInt(GBK_RANGES_HEX.substr(_i+6,4),16)\n';
jsBlock += '  });\n';
jsBlock += '}\n';
jsBlock += '\n';
jsBlock += 'function charToGBK(cp){\n';
jsBlock += '  var lo=0,hi=GR.length-1;\n';
jsBlock += '  while(lo<=hi){\n';
jsBlock += '    var mid=(lo+hi)>>1;\n';
jsBlock += '    var r=GR[mid];\n';
jsBlock += '    if(cp>=r.u && cp<r.u+r.n){\n';
jsBlock += '      return r.g+(cp-r.u);\n';
jsBlock += '    }else if(cp<r.u){\n';
jsBlock += '      hi=mid-1;\n';
jsBlock += '    }else{\n';
jsBlock += '      lo=mid+1;\n';
jsBlock += '    }\n';
jsBlock += '  }\n';
jsBlock += '  return null;\n';
jsBlock += '}\n';
jsBlock += '\n';
// 一个字符 → hex 文本 (英文 2 位, 汉字 4 位); 码表里没有的返回 null
jsBlock += 'function charHex(ch){\n';
jsBlock += '  var cp=ch.codePointAt(0);\n';
jsBlock += '  if(cp<0x80)return (cp<16?"0":"")+cp.toString(16);\n';
jsBlock += '  var g=charToGBK(cp);\n';
jsBlock += '  return g===null?null:g.toString(16).padStart(4,"0");\n';
jsBlock += '}\n';

// 页面本体
var html = `<!DOCTYPE html>
<html>
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width,initial-scale=1,user-scalable=no">
<title>ESP32 打字桥</title>
<style>
*{box-sizing:border-box;margin:0;padding:0}
body{font-family:system-ui,-apple-system,sans-serif;background:#1a1a2e;color:#eee;padding:12px;min-height:100vh}
h2{text-align:center;margin-bottom:10px;color:#00d4aa;font-size:1.2em}
.card{background:#16213e;border-radius:10px;padding:14px;margin-bottom:10px}
label{display:block;margin-bottom:4px;color:#888;font-size:0.85em}
textarea{width:100%;height:160px;background:#0f3460;color:#eee;border:1px solid #333;border-radius:6px;padding:10px;font-size:16px;resize:vertical}
input[type=range]{width:100%;margin:4px 0;accent-color:#00d4aa}
.btn{width:100%;padding:13px;border:none;border-radius:6px;font-size:16px;font-weight:bold;cursor:pointer;margin-top:6px}
.b1{background:#00d4aa;color:#1a1a2e}
.b2{background:#e94560;color:#fff}
.b3{background:#533483;color:#fff}
.spd{text-align:center;color:#00d4aa;font-size:0.9em}
.st{text-align:center;padding:7px;border-radius:6px;background:#0f3460;font-size:0.85em;margin-top:8px}
.tip{color:#888;font-size:0.8em;margin-top:8px;line-height:1.5}
.row{display:flex;gap:6px}
.row .btn{flex:1}
</style>
</head>
<body>
<h2>ESP32 打字桥</h2>
<div class="card">
<label>要打的文字：</label>
<textarea id="t" placeholder="在这里输入或粘贴文字"></textarea>
<label>按键间隔（毫秒，越大越慢越稳）：</label>
<input type="range" id="s" min="10" max="100" value="20">
<div class="spd" id="sv">20 毫秒</div>
<div class="row">
<button class="btn b1" onclick="typeText()">键盘打字</button>
<button class="btn b2" onclick="stopTyping()">停止</button>
</div>
<div class="row">
<button class="btn b3" onclick="sendToPC()">发送到电脑程序</button>
<button class="btn b3" onclick="ta.value=''">清空</button>
</div>
<div class="st" id="st">就绪</div>
<div class="tip">打字期间，电脑上的光标要一直停在要输入的位置，不要点别的窗口。</div>
</div>
<script>
var sl=document.getElementById('s'),sv=document.getElementById('sv'),
    st=document.getElementById('st'),ta=document.getElementById('t');
sl.oninput=function(){sv.textContent=sl.value+' 毫秒'};

${jsBlock}
var SEG=100;
var running=false,stopFlag=false;

function sleep(ms){return new Promise(function(r){setTimeout(r,ms)})}

function show(msg,color){st.textContent=msg;st.style.background=color||'#0f3460';}

async function req(method,url,body){
  var c=new AbortController();
  var tm=setTimeout(function(){c.abort()},5000);
  try{
    var opt={method:method,signal:c.signal};
    if(body!==undefined){opt.headers={'Content-Type':'application/x-www-form-urlencoded'};opt.body=body;}
    var r=await fetch(url,opt);
    var txt=await r.text();
    return {status:r.status,text:txt,left:parseInt(txt.split(' ')[1])||0};
  }finally{clearTimeout(tm);}
}

async function typeText(){
  if(running)return;
  var chars=[...ta.value.replace(/\\r/g,'')];
  var hexes=[],skipped=0;
  for(var i=0;i<chars.length;i++){
    var h=charHex(chars[i]);
    if(h===null)skipped++;else hexes.push(h);
  }
  if(!hexes.length){show('没有可以打的字','#e94560');return;}
  var segs=[];
  for(var i=0;i<hexes.length;i+=SEG)segs.push(hexes.slice(i,i+SEG).join(''));
  var job=Math.floor(Math.random()*1000000000)+1;
  running=true;stopFlag=false;
  try{
    for(var n=0;n<segs.length&&!stopFlag;n++){
      var fails=0;
      while(!stopFlag){
        show('发送中 '+(n+1)+'/'+segs.length,'#e94560');
        var r=null;
        try{r=await req('POST','/send','m=K&j='+job+'&n='+n+'&s='+sl.value+'&h='+segs[n]);}
        catch(e){
          if(++fails>=10)throw new Error('连不上板子（已重试 10 次）');
          await sleep(1000);continue;
        }
        if(r.status===200)break;
        if(r.status===503){show('板子队列满了，等它打一会儿…','#e94560');await sleep(1000);continue;}
        throw new Error('板子回复 '+r.status+' '+r.text);
      }
    }
    var fails=0;
    while(!stopFlag){
      try{
        var r=await req('GET','/status');
        fails=0;
        if(r.left===0)break;
        show('打字中，还剩 '+r.left+' 字','#e94560');
      }catch(e){
        if(++fails>=10)throw new Error('打字途中连不上板子了');
      }
      await sleep(1000);
    }
    if(!stopFlag){
      show('完成：共 '+hexes.length+' 字'+(skipped?'（跳过 '+skipped+' 个打不出的字符，如表情）':''),'#00d4aa');
      ta.value='';
    }
  }catch(e){show('出错：'+e.message,'#e94560');}
  running=false;
}

async function stopTyping(){
  stopFlag=true;
  try{await req('POST','/stop','');show('已停止','#533483');}
  catch(e){show('停止指令没送到板子：'+e.message,'#e94560');}
}

async function sendToPC(){
  var t=ta.value;
  if(!t)return;
  try{
    var r=await req('POST','/send','m=C&t='+encodeURIComponent(t));
    show(r.text,r.status===200?'#00d4aa':'#e94560');
    if(r.status===200)ta.value='';
  }catch(e){show('出错：'+e.message,'#e94560');}
}

setInterval(async function(){
  if(running)return;
  try{
    var r=await req('GET','/poll');
    if(r.text.length>0){
      show('来自电脑：'+r.text.substring(0,40),'#533483');
      ta.value=r.text;
    }
  }catch(e){}
},2000);
</script>
</body>
</html>
`;

// Build the C header
var output = '#pragma once\n\n';
output += '// 由 tools/build_html_page.js 生成, 不要直接修改本文件\n';
output += '// GBK compressed table: ' + (compressedHex.length / 10) + ' ranges, covers 23939 chars\n';
output += 'const char INDEX_HTML[] PROGMEM = R"rawliteral(' + html + ')rawliteral";\n';

fs.writeFileSync(OUT_PATH, output);

var stats = fs.statSync(OUT_PATH);
console.log('html_page.h written: ' + stats.size + ' bytes (' + (stats.size / 1024).toFixed(1) + ' KB)');
