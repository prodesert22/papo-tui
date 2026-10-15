"""Emoji and avatar screenshots in disposable native Kitty/Konsole windows."""
import argparse
import json
from pathlib import Path
import sys
import tempfile
import time
from tools.check_backend import private_write
from tools.check_r3 import ROOT
from tools.check_terminals import Emulator, wait_for
from tools.check_media_terminals import screenshot, colored_pixels


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary',type=Path,default=ROOT/'build/papo-tui')
    parser.add_argument('--output',type=Path,default=ROOT/'build/emojis-terminals')
    parser.add_argument('--terminal',action='append',choices=('Kitty','Konsole'))
    args=parser.parse_args(); work=args.output.resolve(); work.mkdir(parents=True,exist_ok=True,mode=0o700)
    saved=sys.argv[:]; sys.path.insert(0,str(ROOT/'tests')); sys.argv=[sys.argv[0],str(args.binary.resolve()),'ffmpeg']
    try:import test_emojis_terminal as fixtures
    finally:sys.argv=saved
    checks={}; stage='setup'; emulator=None
    fixtures.EmojiTerminal.setUpClass()
    try:
        helper=fixtures.EmojiTerminal()
        for name in args.terminal or ('Kitty','Konsole'):
            stage=name+' startup'
            with helper.server(content='Emojis 😄 :gato 界:',count=2,animated=True) as server:
                with tempfile.TemporaryDirectory(prefix='papo-emoji-emulator-') as directory:
                    emulator=Emulator(name,args.binary.resolve(),Path(directory),None)
                    try:
                        emulator.resize(False)
                        emulator.send(server.url+'\tjosé\tSynthetic!123\n')
                        wait_for(lambda:'geral' in emulator.screen()); emulator.send('\x1bOB\x1bOB\n')
                        wait_for(lambda:'Mensagem' in emulator.screen() or 'Emojis' in emulator.screen())
                        stage=name+' native pixels'
                        checks[name+'_red_frame']=bool(wait_for(lambda:colored_pixels(emulator,work/(name.lower()+'-history.png'),'red')>80))
                        checks[name+'_blue_frame']=bool(wait_for(lambda:colored_pixels(emulator,work/(name.lower()+'-animated.png'),'blue')>80))
                        emulator.send('\x1bOS'); wait_for(lambda:'Emojis · Esc voltar' in emulator.screen())
                        stage=name+' picker'
                        emulator.send('\t\x1bOC'); wait_for(lambda:'1 emojis do servidor' in emulator.screen())
                        time.sleep(.35)  # Allow the compositor to present the text observed by remote control.
                        screenshot(emulator,work/(name.lower()+'-picker.png'))
                        emulator.send('\t\n'); wait_for(lambda:'Emojis · Esc voltar' not in emulator.screen())
                        checks[name+'_inserts_without_sending']=server.message_count==0
                        emulator.send('\x1bOS');wait_for(lambda:'Emojis · Esc voltar' in emulator.screen())
                        emulator.resize_narrow();wait_for(lambda:emulator.state().get('columns',100)<80)
                        wait_for(lambda:'Emojis · Esc voltar' in emulator.screen());time.sleep(1.1);screenshot(emulator,work/(name.lower()+'-narrow.png'))
                        emulator.click(0,0);wait_for(lambda:'Emojis · Esc voltar' not in emulator.screen())
                        checks[name+'_outside_click_and_resize']=True
                    finally:
                        emulator.close();checks[name+'_terminal_restored']=emulator.state().get('restored',False);emulator=None
        report={'passed':all(checks.values()),'kind':'native-emulator-synthetic-server','checks':checks}
    except Exception:
        if emulator:emulator.close()
        report={'passed':False,'stage':stage,'checks':checks,'error':'Native terminal validation failed'}
    finally:fixtures.EmojiTerminal.tearDownClass()
    private_write(work/'summary.json',report);print(json.dumps(report,ensure_ascii=False));return 0 if report['passed'] else 1

if __name__=='__main__':raise SystemExit(main())
