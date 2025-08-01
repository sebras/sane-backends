#!/bin/sh -eux
# tools/create-release.sh -- via GitLab CI and API
# Copyright (C) 2019  Olaf Meeuwissen
#
# License: GPL-3.0+

PROJECT_ID=$CI_PROJECT_ID

API_ENDPOINT=https://gitlab.com/api/v4

# Uploads a file and returns a project relative URL to it.
upload () {
    curl --silent --fail \
         --header "PRIVATE-TOKEN: $PRIVATE_TOKEN" \
         --form "file=@$1" \
         --request POST \
         $API_ENDPOINT/projects/$PROJECT_ID/uploads \
        | jq --raw-output .url \
        | sed "s|^|https://gitlab.com/-/project/$PROJECT_ID|"
}

cat << EOF > release.json
{
  "name": "SANE Backends $CI_COMMIT_TAG",
  "tag_name": "$CI_COMMIT_TAG",
  "description": $(sed '1,9d;/\x0c/,$d' NEWS \
     | git stripspace \
     | jq -Rs),
  "assets": {
    "links": [
EOF

for check in sha256 sha512; do
    ${check}sum sane-backends-$CI_COMMIT_TAG.tar.gz \
            > sane-backends-$CI_COMMIT_TAG.$check.txt
    cat << EOF >> release.json
      {
        "name": "sane-backends-$CI_COMMIT_TAG.$check.txt",
        "url": "$(upload sane-backends-$CI_COMMIT_TAG.$check.txt)"
      },
EOF
done

cat << EOF >> release.json
      {
        "name": "sane-backends-$CI_COMMIT_TAG.tar.gz",
        "url": "$(upload sane-backends-$CI_COMMIT_TAG.tar.gz)"
      }
    ]
  }
}
EOF

echo '=================================================='
cat release.json
echo '=================================================='

curl --silent --fail --write-out "%{http_code}\n"\
     --header "PRIVATE-TOKEN: $PRIVATE_TOKEN" \
     --header "Content-Type: application/json" \
     --data @release.json \
     --request POST $API_ENDPOINT/projects/$PROJECT_ID/releases
