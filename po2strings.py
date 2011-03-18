# coding=iso-8859-1

import os, sys, StringIO

StateComment = 0
StateMsgstr = 1
StateLongMsgid = 2

class Converter:
	def __init__(self):
		self.comment = ''
		self.state = StateComment 
		self.msgid = ''
		self.msgstr = ''
		self.plural = ''
		self.buf = StringIO.StringIO()
		self.out('/* Automatically generated strings file */\n')

	def next(self):
		self.line = self.src.readline()
		if self.line == '': 
			self.line = None
		else:
			self.line = self.line.strip()
#		sys.stderr.write('A %s\n' % repr(self.line))
		return self.line

	def out(self, text):
		self.buf.write(text)

	def fix(self, text):
#		text = text.replace("'", "''")
#		text = text.replace('"', '""')
#		text = text.replace('\\', '\\\\')
		return text

	def ProcessComment(self):
		self.out('\n/* ')
		self.out(self.line[2:])
		self.out(' */\n')
		self.next()
		self.ProcessMsgid()

	def ProcessString(self):
		result = ''
		while self.line and self.line[:1] == '"':
			result = result + self.line[1:-1]
			self.next()
		return result

	def ProcessMsgid(self):
		while self.line[:1] == '#':
			self.next()

		if self.line[:6] != 'msgid ':
			sys.stderr.write('WARNING: expecting msgid, line "%s"\n' % self.line)
		self.line = self.line[6:]
		self.msgid = self.ProcessString()

		if self.line[:13] == 'msgid_plural ':
			self.ProcessPlural()

		self.ProcessMsgstr()

	def ProcessPlural(self):
		self.line = self.line.split(' ', 1)[1]
		self.plural = self.ProcessString()

	def ProcessMsgstr(self):
		while self.line and self.line[:6] == 'msgstr':
			if self.line[6] == '[' and self.line[7] != '0':
				self.msgid = self.plural + ' {' + self.line[7] + '}'

			self.line = self.line.split(' ', 1)[1]
			self.msgstr = self.ProcessString()
			empty = self.msgstr == ''
			if empty: self.out('/* ')
			self.out('"%s" = "%s";' % (self.fix(self.msgid), self.fix(self.msgstr)))
			if empty: self.out(' */')
			self.out('\n')

	def Process(self, src):
		self.src = src
		self.next()
		while not self.ProcessLine():
			pass

	def ProcessLine(self):
		line = self.line
		if line == None:
			return True

		if line[:2] == '#:':
			self.ProcessComment()
#		elif line[:1] == '"':
#			if self.state == StateLongMsgid:
#				self.msgid = self.msgid + line.strip('"')

		elif (line == '' or line[0] == '#'):
			self.next()

		elif line[:6] == 'msgid ' or line[:7] == 'msgstr ':
			self.next()
			self.ProcessString()


#		elif line[:6] == 'msgid ':
#			if line[6:] == '""':
#				self.msgid = ''
#				self.state = StateLongMsgid 
#			else:
#				self.state = StateMsgstr
#				self.msgid = line[6:].strip('"')
#
#		elif line[:13] == 'msgid_plural ':
#			if self.state != 1:
#				sys.stderr.write('WARNING: msgid_plural not expected\n')
#			self.plural = line[13:].strip('"')
#
#		elif line[:6] == 'msgstr':
#			countstr = ''
#			if line[6] == '[':
#				countstr = ' {' + line[7] + '}'
#				if countstr == '0': countstr = ''
#
#			if self.state == StateLongMsgid:
#				sys.stderr.write('WARNING: skipping "%s"\n' % line)
#				pass
#			elif self.state != StateMsgstr:
#				sys.stderr.write('WARNING: msgstr expected: "%s"\n' % line)
#			else:
#				val = line.split(' ', 1)[1]
#				if val == '""': self.out('/* ')
#				self.out('"')
#				self.out(self.msgid)
#				self.out('%s" = ' % countstr)
#				self.out(val)
#				if val == '""': self.out(' */ ')
#				self.out(';\n')

		else:
			sys.stderr.write('WARNING: unsupported line: "%s"\n' % line)

		return False

	def Result(self):
		return self.buf.getvalue()

src = open(sys.argv[1])

conv = Converter()
#while True:
#	if conv.ProcessLine(src):
#		break
conv.Process(src)

s = conv.Result()
s = s.decode('utf-8')
s = s.encode('utf-16')
sys.stdout.write(s)

src.close()

