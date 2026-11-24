#!/usr/bin/env python3
"""Bundles three.js (offline) into the Lantern Sprite Baker single HTML file."""
import sys, os
T = sys.argv[1]  # path to three.js checkout
here = os.path.dirname(os.path.abspath(__file__))
parts = {
    'src-core': 'build/three.core.js', 'src-three': 'build/three.module.js',
    'src-bgu': 'examples/jsm/utils/BufferGeometryUtils.js', 'src-skel': 'examples/jsm/utils/SkeletonUtils.js',
    'src-gltf': 'examples/jsm/loaders/GLTFLoader.js', 'src-ply': 'examples/jsm/loaders/PLYLoader.js', 'src-obj': 'examples/jsm/loaders/OBJLoader.js',
}
blocks = []
for k, p in parts.items():
    s = open(os.path.join(T, p), encoding='utf-8').read()
    assert '</script' not in s.lower(), p
    blocks.append(f'<script type="text/plain" id="{k}">{s}</script>')
lic = open(os.path.join(T, 'LICENSE')).read()
html = open(os.path.join(here, 'baker_template.html'), encoding='utf-8').read()
html = html.replace('<!--THREE_SOURCES-->', '<!-- three.js (MIT License) bundled for offline use:\n' + lic.replace('--', '- -') + '\n-->\n' + '\n'.join(blocks))
out = sys.argv[2]
open(out, 'w', encoding='utf-8').write(html)
print('wrote', out, len(html) // 1024, 'KB')
