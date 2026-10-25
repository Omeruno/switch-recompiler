#!/usr/bin/env python3
"""Validate the offline roadmap's embedded data and local document references."""
import datetime
import json
from html.parser import HTMLParser
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parent.parent


class RoadmapParser(HTMLParser):
    def __init__(self):
        super().__init__()
        self.capture = False
        self.blocks = []

    def handle_starttag(self, tag, attrs):
        attrs = dict(attrs)
        if tag == 'script':
            assert 'src' not in attrs, 'Roadmap must work without external scripts'
            self.capture = attrs.get('id') == 'roadmap-data'
            if self.capture:
                self.blocks.append('')
        if tag == 'link':
            assert attrs.get('rel') != 'stylesheet', 'Keep styles embedded for offline use'

    def handle_endtag(self, tag):
        if tag == 'script':
            self.capture = False

    def handle_data(self, text):
        if self.capture:
            self.blocks[-1] += text


def main():
    parser = RoadmapParser()
    parser.feed((ROOT / 'docs/roadmap/index.html').read_text(encoding='utf-8'))
    assert len(parser.blocks) == 1, 'Expected one roadmap-data block'
    data = json.loads(parser.blocks[0])
    assert data['schema'] == 1
    datetime.date.fromisoformat(data['updated'])
    assert re.fullmatch(r'[0-9a-f]{40}', data['runtimeSha'])
    assert data['repo'] == 'https://github.com/Omeruno/switch-recompiler'
    assert data['ciUrl'].startswith(data['repo'] + '/actions/runs/')
    states = {'base', 'accepted', 'partial', 'prototype', 'current', 'planned',
              'deferred', 'ongoing', 'tool'}
    ids = [n['id'] for n in data['nodes']]
    assert len(ids) == len(set(ids)), 'Duplicate card IDs'
    assert data['current'] in ids
    docs = set()
    for node in data['nodes']:
        assert node['status'] in states
        for field in ('code', 'title', 'summary', 'gate', 'kind'):
            assert isinstance(node[field], str) and node[field].strip(), (node['id'], field)
        for field in ('done', 'todo', 'docs'):
            assert isinstance(node[field], list)
            assert all(isinstance(s, str) and s.strip() for s in node[field])
        assert node['docs'], 'Cards must link to supporting documents'
        for path in node['docs']:
            assert path.startswith('docs/') and '..' not in Path(path).parts
            assert (ROOT / path).is_file(), f'Missing document: {path}'
            docs.add(path)
    views = data['views']
    assert len({v['id'] for v in views}) == len(views)
    visible = set()
    for view in views:
        members = [n['id'] for n in view['nodes']]
        assert len(members) == len(set(members))
        assert set(members) <= set(ids)
        assert sorted(view['mobileOrder']) == sorted(members)
        assert all(isinstance(n[k], (int, float)) and n[k] >= 0
                   for n in view['nodes'] for k in ('x', 'y'))
        edges = [(e['from_'], e['to']) for e in view['edges']]
        assert len(edges) == len(set(edges))
        for edge in view['edges']:
            assert edge['from_'] in members and edge['to'] in members
            assert edge['from_'] != edge['to']
            assert edge['kind'] in ('main', 'extra')
        visible.update(members)
    assert visible == set(ids), 'Every card needs a location on a map'
    print(f'Roadmap OK: {len(ids)} cards, {len(views)} views, {len(docs)} existing documents')


if __name__ == '__main__':
    main()
