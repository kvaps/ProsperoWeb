#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Print the ProsperoStore catalog record for a published release.
#
#   scripts/store-record.sh <tag>           # print the record to stdout
#   scripts/store-record.sh <tag> --write   # also update docs/store/<TITLEID>.json
#
# The sha256 always comes from GitHub's digest of the published asset, never
# from a local build: the store compares the downloaded bytes against the
# record (src/install/transaction.cpp in darkxex/ProsperoStore), so the hash
# is only correct once the final asset is attached to the release. Run this
# after the release workflow has attached <TITLEID>.zip and the release is
# published.
#
# Requires: gh (authenticated), jq.
set -euo pipefail

die() { echo "store-record: $*" >&2; exit 1; }

TAG="${1:?"usage: store-record.sh <tag> [--write]"}"
shift
WRITE=0
[[ "${1:-}" == "--write" ]] && WRITE=1

[[ "${TAG}" =~ ^v[0-9]+\.[0-9]+\.[0-9]+$ ]] || die "tag must look like v0.3.1, got '${TAG}'"

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "${ROOT}"

REPO="$(gh repo view --json nameWithOwner -q .nameWithOwner)" \
    || die "gh repo view failed; run inside the ProsperoWeb checkout"

BASE="docs/store/PRWB00001.json"
[[ -f "${BASE}" ]] || die "base record ${BASE} is missing"

TITLE_ID="$(jq -er .titleid "${BASE}")" || die "${BASE} has no titleid"

release="$(gh api "repos/${REPO}/releases/tags/${TAG}")" \
    || die "no release ${TAG} in ${REPO}; tag and publish it first"

published="$(jq -r .published_at <<<"${release}")"
[[ "${published}" != "null" && -n "${published}" ]] \
    || die "release ${TAG} is still a draft; publish it first (the digest is only final once the asset is up)"

asset="$(jq -c --arg name "${TITLE_ID}.zip" '.assets[] | select(.name == $name)' <<<"${release}")"
[[ -n "${asset}" ]] || die "release ${TAG} has no ${TITLE_ID}.zip asset yet"

digest="$(jq -r .digest <<<"${asset}")"
[[ "${digest}" =~ ^sha256:[0-9a-f]{64}$ ]] || die "GitHub reports no sha256 digest for ${TITLE_ID}.zip"

ICON_URL="https://raw.githubusercontent.com/${REPO}/${TAG}/assets/sce_sys/icon0.png"

record="$(
    jq -n \
        --argjson base "$(cat "${BASE}")" \
        --arg repo "${REPO}" \
        --arg version "${TAG#v}" \
        --arg artifact_url "https://github.com/${REPO}/releases/download/${TAG}/${TITLE_ID}.zip" \
        --arg sha256 "${digest#sha256:}" \
        --arg icon_url "${ICON_URL}" \
        '{
            titleid: $base.titleid,
            name: $base.name,
            kind: $base.kind,
            category: $base.category,
            description: $base.description,
            license: $base.license,
            author: $base.author,
            version: $version,
            source_repo: ("https://github.com/" + $repo),
            artifact_url: $artifact_url,
            sha256: $sha256,
            icon_url: $icon_url
        }'
)"

# The icon must be reachable at the tag for the catalog CI to accept it.
icon_status="$(curl -s -o /dev/null -w '%{http_code}' --max-time 15 -I "${ICON_URL}")"
[[ "${icon_status}" == "200" ]] \
    || echo "store-record: warning: icon_url answered HTTP ${icon_status}" >&2

if [[ "${WRITE}" == "1" ]]; then
    tmp="$(mktemp "${BASE}.XXXXXX")"
    jq . <<<"${record}" >"${tmp}" && mv "${tmp}" "${BASE}"
fi

jq . <<<"${record}"
