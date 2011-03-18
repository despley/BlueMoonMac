import os, sys, re

# Find version number of the app
f = open('Info.plist')
info = f.read()
f.close()
res = re.search('<key>CFBundleVersion</key>\s*<string>(.+?)</string>', info, re.M)
if not res:
    print "Version information not found"
    sys.exit(-1)
version = res.group(1)

# Display info text
text = 'Building Blue Moon %s release packages' % version
print "=" * len(text)
print text
print "=" * len(text)
print

# Clean and build the project
os.system('xcodebuild -configuration Release clean')
os.system('xcodebuild -configuration Release build')

# Create binary package
file = '~/Desktop/bluemoon-mac-%s.zip' % version
os.system('cd build/Release && rm -f %s && zip -r %s Blue\ Moon.app' % (file, file))

# Create source package
file = '~/Desktop/bluemoon-mac-src-%s.zip' % version
os.system('cd .. && rm -f %s && zip -r %s mac -x mac/build/\* \*/.\* \*.pbxuser \*.mode1v3' % (file, file))
