module.exports = {
    "extends": "airbnb-base",
    "env": {
        "browser": true,
        "node": true,
        "mocha": true,
    },
    "rules": {
        "indent": ["error", 4],
        "no-plusplus": ["error", { "allowForLoopAfterthoughts": true }],
        "no-console": "off",
        "no-await-in-loop": "off",
        "no-unused-expressions": "off",

        "no-restricted-syntax": ["error", "ForInStatement", "LabeledStatement", "WithStatement"],
        "no-param-reassign": ["error", { "props": false }]
    },

    "parser": "babel-eslint",
    "parserOptions": {
        "sourceType": "module",
        "allowImportExportEverywhere": true
    },
}
