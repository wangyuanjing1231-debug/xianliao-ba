#!/usr/bin/env python3
import sys, json, time, os, re

event = sys.argv[1] if len(sys.argv) > 1 else "unknown"
raw = sys.stdin.read()
data = {}
if raw:
    try:
        data = json.loads(raw)
    except Exception:
        pass

def get_task_progress(data):
    artifact_dir = data.get("artifactDirectoryPath")
    if not artifact_dir:
        conv_id = data.get("conversationId")
        if conv_id:
            artifact_dir = f"/root/.gemini/antigravity-cli/brain/{conv_id}"
    base = "/root/.gemini/antigravity-cli/brain"
    if (not artifact_dir or not os.path.isdir(artifact_dir)) and os.path.isdir(base):
        try:
            subdirs = [os.path.join(base, d) for d in os.listdir(base) if not d.startswith(".")]
            subdirs = [d for d in subdirs if os.path.isdir(d)]
            if subdirs:
                artifact_dir = max(subdirs, key=os.path.getmtime)
        except Exception:
            pass
    if not artifact_dir or not os.path.isdir(artifact_dir):
        return 
    candidate_files = []
    try:
        for fname in os.listdir(artifact_dir):
            if fname.endswith(".md") and not fname.startswith("."):
                candidate_files.append(os.path.join(artifact_dir, fname))
        scratch = os.path.join(artifact_dir, "scratch")
        if os.path.isdir(scratch):
            for fname in os.listdir(scratch):
                if fname.endswith(".md") and not fname.startswith("."):
                    candidate_files.append(os.path.join(scratch, fname))
    except Exception:
        pass

    pattern = re.compile(r'^\s*(?:[-*+]|\d+\.)\s*\[([ xX])\]')
    for fpath in candidate_files:
        try:
            total = 0
            completed = 0
            with open(fpath, "r", encoding="utf-8", errors="ignore") as f:
                for line in f:
                    m = pattern.search(line)
                    if m:
                        total += 1
                        if m.group(1) in ('x', 'X'):
                            completed += 1
            if total > 0:
                return f"{completed}/{total}"
        except Exception:
            pass
    return ""

state = "busy"
step = 0
tool = "none"
summary = ""

if event == "PreInvocation":
    state = "busy"
    step = data.get("initialNumSteps", 0)
    tool = "thinking"
    summary = "Thinking..."
elif event == "PreToolUse":
    state = "busy"
    step = data.get("stepIdx", 0)
    tc = data.get("toolCall", {})
    tool = tc.get("name", tool)
    args = tc.get("args", {})
    summary = args.get("toolSummary") or args.get("toolAction") or tool
elif event == "PostToolUse":
    state = "busy"
    step = data.get("stepIdx", 0)
    tc = data.get("toolCall", {})
    tool = tc.get("name", tool)
    summary = "Step completed"
elif event == "PostInvocation":
    state = "busy"
    step = data.get("initialNumSteps", 0)
    tool = "thinking"
    summary = "Thinking..."
elif event == "Stop":
    state = "done"
    summary = "Task finished"
elif event == "SessionStart":
    state = "busy"
    summary = "Session starting"

progress = get_task_progress(data)

out = {
    "state": state,
    "progress": progress,
    "step": step,
    "tool": tool,
    "summary": summary,
    "event": event,
    "time": int(time.time()),
    "ts": time.strftime("%Y-%m-%d %H:%M:%S")
}

try:
    with open("/root/desk-pet-state.json.tmp", "w") as f:
        json.dump(out, f, ensure_ascii=False)
    os.replace("/root/desk-pet-state.json.tmp", "/root/desk-pet-state.json")
except Exception:
    pass

if event == "PreToolUse":
    print(json.dumps({"decision":"allow"}))
elif event == "Stop":
    print(json.dumps({"decision":"stop"}))
else:
    print(json.dumps({}))
