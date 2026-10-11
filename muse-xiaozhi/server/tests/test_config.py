import os

import main


def test_user_config_overrides_defaults(tmp_path):
    p = tmp_path / "config.yaml"
    p.write_text("server:\n  api_token: abc\nmuse:\n  session_id: s1\n", encoding="utf-8")
    cfg = main.load_config(str(p))
    assert cfg["server"]["api_token"] == "abc"
    assert cfg["server"]["port"] == 8800          # 没写的项保留默认
    assert cfg["muse"]["session_id"] == "s1"
    assert cfg["muse"]["command"] == "musegadget"
    assert "{path}" in cfg["muse"]["prompt_template"]


def test_example_config_paths_exist_in_repo_layout():
    cfg = main.load_config(os.devnull)
    assert cfg["speech"]["asr"]["model_dir"].startswith("models/")
