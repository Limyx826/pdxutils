#!/usr/bin/env python3


# REMEMBER TO:
# copy EMF's emf_core_custom_loc.txt and 1_emf_debug.csv
# delete 00_province_setup.txt


import re
import ck2parser
from print_time import print_time


NEW_DATA_FROM_FILE = ck2parser.rootpath / 'province_setup_data.txt'
# NEW_DATA_FROM_FILE = None # format only


@print_time
def main():
    parser = ck2parser.FullParser(ck2parser.rootpath / 'SWMH-BETA/SWMH')
    if NEW_DATA_FROM_FILE:
        output_tree = parser.parse('')
        with NEW_DATA_FROM_FILE.open(encoding='utf8') as f:
            for line in f:
                match = re.match(r'<(.*?)> (.*)', line)
                prov_type, pairs = match.groups()
                data = dict(x.split('=') for x in pairs.split(', '))
                to_parse = '{} = {{\n'.format(data['id'])
                if data.get('title'):
                    to_parse += 'title = {}\n'.format(data['title'])

                to_parse += 'max_settlements = {}\n'.format(
                    data['max_settlements'] if prov_type == 'LAND' else 7)
                to_parse += 'terrain = {}\n'.format(data['terrain'])
                to_parse += '}\n'
                parsed = parser.parse(to_parse)
                output_tree.contents.append(parsed.contents[0])
        header = '# -*- ck2.province_setup -*-'
        parsed = parser.parse(header)
        output_tree.contents[0].key.pre_comments = parsed.post_comments
    else:
        output_tree = parser.parse_file('common/province_setup/'
                                        '00_province_setup.txt')
    outpath = parser.moddirs[0] / 'common/province_setup/00_province_setup.txt'
    parser.write(output_tree, outpath)


if __name__ == '__main__':
    main()
