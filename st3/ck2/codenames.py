#!/usr/bin/python3

import sys
import argparse
import re
import ck2parser
from collections import defaultdict
from pathlib import Path

g_id_output_dir = ck2parser.rootpath / 'ck2utils/st3/ck2/var'
g_emf_path = ck2parser.rootpath / 'EMF/EMF'
g_emf_swmh_path = ck2parser.rootpath / 'EMF/EMF+SWMH'
g_swmh_path = ck2parser.rootpath / 'SWMH-BETA/SWMH'

###


class VarType:
  def __init__(self, name, globs, nest=0, exclude=None, include=None,
               content_filter=None, moddirs=(g_emf_path,), hidden=False):
    self.name = name
    self.globs = list(globs)
    self.nest = nest
    self.include = [] if not include else list(include)
    self.exclude = [] if not exclude else list(exclude)
    self.content_filter = content_filter
    self.moddirs = moddirs
    self.hidden = hidden


class HasPairFilter:
  def __init__(self, lhs, rhs, regexp=False):
    self.lhs = lhs
    self.rhs = rhs
    self.regexp = regexp

  def __call__(self, tree):
    for n, v in tree:
      try:
        if self.regexp and re.search(self.lhs, n.val) and re.search(self.rhs, v.val):
          return True
        elif self.lhs == n.val and self.rhs == v.val:
          return True
      except:
        pass
    return False


class HasPairsFilter:
  def __init__(self, pairs, regexp=False):
    self.pairs = list(pairs)
    self.regexp = regexp

  def __call__(self, tree):
    pairs_left = len(self.pairs)
    for lhs, rhs in self.pairs:
      for n, v in tree:
        try:
          if self.regexp and re.search(lhs, n.val) and re.search(rhs, v.val):
            pairs_left -= 1
          elif lhs == n.val and rhs == v.val:
            pairs_left -= 1
        except:
          pass
    return pairs_left == 0


def parse_var_type_recursive(tree, vt, nest):
  assert nest >= 0
  id_set = set()
  for n, v in tree:
    if isinstance(v, ck2parser.Obj):
      if nest == 0:  # Base case
        if vt.exclude and any(re.search(p, n.val) for p in vt.exclude):
          continue
        if not vt.include or any(re.search(p, n.val) for p in vt.include):
          if not vt.content_filter or vt.content_filter(v):
            id_set.add(n.val)
      else:  # Recursive
        id_set |= parse_var_type_recursive(v, vt, nest - 1)
  return id_set


def parse_var_type(parser, vt):
  parser.moddirs = vt.moddirs
  id_set = set()
  for g in vt.globs:
    for fn, tree in parser.parse_files(g):
      id_set |= parse_var_type_recursive(tree, vt, vt.nest)
  return id_set


def print_ids(ids, loc, file=sys.stdout):
  max_len = 0
  for i in ids:
    if len(i) > max_len:
      max_len = len(i)
  for i in sorted(ids):
    loc_val = loc.get(i)
    if loc_val:
      print('{}{} # {}'.format(i, (max_len - len(i)) * ' ', loc_val), file=file)
    else:
      print(i, file=file)



def main():
  relcul_ignore = (r'color', r'color2', r'(fe)?male_names', r'interface_skin', r'alternate_start', r'(unit_)?graphical_cultures', r'(character|unit(_home_)?)modifier', r'trigger$')
  strigger_globs = ['common/scripted_triggers/' + p for p in ('emf_*trigger*.txt', 'hip_*trigger*.txt', 'plus_*trigger*.txt')]
  seffect_globs = ['common/scripted_effects/' + p for p in ('emf_*effect*.txt', 'hip_*effect*.txt', 'plus_*effect*.txt')]
  vtype_list = [
    VarType('Religion', ['common/religions/*.txt'], nest=1, exclude=relcul_ignore),
    VarType('ReligionGroup', ['common/religions/*.txt'], exclude=relcul_ignore),
    VarType('Culture', ['common/cultures/*.txt'], nest=1, exclude=relcul_ignore, moddirs=(g_swmh_path, g_emf_path, g_emf_swmh_path)),
    VarType('CultureGroup', ['common/cultures/*.txt'], exclude=relcul_ignore, moddirs=(g_swmh_path, g_emf_path, g_emf_swmh_path)),
    VarType('CB', ['common/cb_types/*.txt']),
    VarType('ReligionFeature', ['common/religion_features/*.txt'], nest=1, exclude=['buttons']),
    VarType('MinorTitle', ['common/minor_titles/*.txt', 'common/religious_titles/*.txt']),
    VarType('JobTitle', ['common/job_titles/*.txt']),
    VarType('JobAction', ['common/job_actions/*.txt']),
    VarType('Law', ['common/laws/*.txt'], nest=1, exclude=[r'^dynlevy']),
    VarType('Building', ['common/buildings/*.txt'], nest=1, moddirs=(g_swmh_path, g_emf_path, g_emf_swmh_path)),
    VarType('Tech', ['common/technology.txt'], nest=1),
    VarType('Trait', ['common/traits/*.txt']),
    VarType('CachedTrait', ['common/traits/*.txt'], content_filter=HasPairFilter('cached', 'yes')),
    VarType('Region', ['map/geographical_region.txt'], moddirs=(g_swmh_path, g_emf_path, g_emf_swmh_path)),
    VarType('Climate', ['map/climate.txt']),
    VarType('Terrain', ['map/terrain.txt'], nest=1, exclude=['color'], moddirs=(g_swmh_path, g_emf_path, g_emf_swmh_path)),
    VarType('ScriptedTrigger', strigger_globs, exclude=(r'^impl_',)),
    VarType('ScriptedEffect', seffect_globs, exclude=(r'^emf_notify_',)),
    VarType('ScriptedScore', ['common/scripted_score_values/*.txt']),
    VarType('Faction', ['common/objectives/*.txt'], include=[r'^faction_']),
    VarType('Ambition', ['common/objectives/*.txt'], include=[r'^obj_']),
    VarType('Plot', ['common/objectives/*.txt'], include=[r'^plot_']),
    VarType('Focus', ['common/objectives/*.txt'], include=[r'^focus_']),
    VarType('OnAction', ['common/on_actions/*.txt'], moddirs=()),
    VarType('Government', ['common/governments/*.txt'], nest=1),
    VarType('Society', ['common/societies/*.txt']),
    VarType('RetinueType', ['common/retinue_subunits/*.txt'], moddirs=(g_swmh_path, g_emf_path, g_emf_swmh_path)),
    VarType('Nickname', ['common/nicknames/*.txt'], moddirs=(g_swmh_path, g_emf_path, g_emf_swmh_path)),
    VarType('RelHeadTitle', ['common/landed_titles/*.txt'], content_filter=HasPairFilter(r'^controls_religion$', r'^"?[\w-]+"?$', regexp=True), hidden=True),
    VarType('RelHeadTitleLandless', ['common/landed_titles/*.txt'], content_filter=HasPairsFilter([(r'^controls_religion$', r'^"?[\w-]+"?$'), (r'^landless$', r'^yes$')], regexp=True), hidden=True),
  ]

  vtype_by_name = {v.name: v for v in vtype_list}
  argprs = argparse.ArgumentParser(description='List script-defined CK2 identifiers')
  grp = argprs.add_mutually_exclusive_group(required=True)
  grp.add_argument('-t', '--type', help='type of ID to list', choices=vtype_by_name.keys())
  grp.add_argument('-a', '--all', action='store_true', help='write all ID types to files in {}'.format(g_id_output_dir.relative_to(Path.cwd())))
  args = argprs.parse_args()

  parser = ck2parser.SimpleParser()
  loc = ck2parser.get_localisation(moddirs=(g_swmh_path, g_emf_path, g_emf_swmh_path))

  if args.all:
    for vt in vtype_list:
      if not vt.hidden:
        with (g_id_output_dir / (vt.name + '.txt')).open('w', encoding='cp1252', newline='\n') as out_file:
          print_ids(parse_var_type(parser, vt), loc, out_file)
  else:
    ids = parse_var_type(parser, vtype_by_name[args.type])
    print_ids(ids, loc)

  return 0


###


if __name__ == '__main__':
  sys.exit(main())
