# To be used with PyInstaller
# Command line: pyinstaller pyinstaller_setup_waf_onedir.spec

a = Analysis(['cry_waf'],
             hiddenimports=[

						 #misc
						 'platform',
						 'ConfigParser',
						 'uuid',
						 'shlex',
						 'argparse',
						 'codecs',
						 'encodings',
						 'shutil',
						 'Queue',
						 'glob',
						 
						 #xml
						 'xml.etree',
						 'xml.dom',
						 'xml.dom.minidom',
						 'xml.dom.domreg',
						 'xml.dom.minicompat',
						 'xml.dom.xmlbuilder',						 
						 'xml.dom.NodeFilter',
						 'xml.dom.expatbuilder',
						 'xml.dom.pulldom',
						 'xml.sax',
	           'xml.sax.handler'						 
					   ],
             runtime_hooks=None)

# [EXAMPLE]
# Manually remove entire packages		
#a.excludes = [x for x in a.pure if x[0].startswith("waflib")]

# [EXAMPLE]
# Add a single missing dll
#a.binaries = a.binaries + [
#  ('opencv_ffmpeg245_64.dll', 'C:\\Python27\\opencv_ffmpeg245_64.dll', 'BINARY')]	
	
# [EXAMPLE]
## Remove specific binaries
#a.binaries = a.binaries - TOC([
# ('sqlite3.dll', '', ''),
# ('_sqlite3', '', ''),
# ('_ssl', '', '')])
 
# Remove clashing dependency duplicate 
for d in a.datas:
	if 'pyconfig' in d[0]: 
		a.datas.remove(d)
		break
				
pyz = PYZ(a.pure)
exe = EXE(pyz,
          a.scripts,
          name= 'cry_waf.exe',
          debug=False,
          strip=None,
          upx=True,
          console=True,
		  exclude_binaries=True,
		  icon='Icon.ico')
					
coll = COLLECT(exe,
               a.binaries,
               a.zipfiles,
               a.datas,
               strip=None,
               upx=True,
               name='cry_waf')
