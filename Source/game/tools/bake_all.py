#!/usr/bin/env python3
"""Drives the Lantern Sprite Baker headlessly: bakes every model in assets/recipes.json into art/<name>__WxH.png."""
import asyncio, base64, json, os, sys
from playwright.async_api import async_playwright

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BAKER = os.path.abspath(sys.argv[1]) if len(sys.argv) > 1 else os.path.join(ROOT, 'build', 'LanternSpriteBaker.html')
ASSETS = os.path.join(ROOT, 'assets'); ART = os.path.join(ROOT, 'art')
only = set(sys.argv[2:])

async def main():
    os.makedirs(ART, exist_ok=True)
    recipes = json.load(open(os.path.join(ASSETS, 'recipes.json')))
    async with async_playwright() as p:
        b = await p.chromium.launch(args=['--use-angle=swiftshader', '--enable-unsafe-swiftshader'])
        pg = await b.new_page(viewport={'width': 1300, 'height': 900})
        errs = []; pg.on('pageerror', lambda e: errs.append(str(e)))
        await pg.goto('file://' + BAKER)
        await pg.wait_for_function('window.__bakerReady === true', timeout=60000)
        loaded = None
        for r in recipes:
            if only and not any(r['name'] == o or r['name'].startswith(o + '_') for o in only): continue
            if loaded != r['file']:
                n0 = await pg.evaluate('window.__bakeCount || 0')
                await pg.set_input_files('#file', os.path.join(ASSETS, r['file']))
                await pg.wait_for_function(f'(window.__bakeCount || 0) > {n0}', timeout=60000)
                loaded = r['file']
            url = await pg.evaluate('(r) => window.__baker.applyRecipe(r)', r)
            fn = f"{r['name']}__{r['w']}x{r['h']}" + (f"_o{r['fox']}x{r['foy']}" if r.get('anchor') == 'fixed' else '') + '.png'
            open(os.path.join(ART, fn), 'wb').write(base64.b64decode(url.split(',')[1]))
            print('baked', fn)
        if errs: print('page errors:', errs[:5])
        await b.close()
asyncio.run(main())
