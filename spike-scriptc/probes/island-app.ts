import { dispatch, probeFetch } from "fakeapp";
console.log(dispatch("ping", { hello: 1 }));
console.log(dispatch("bump"));
console.log(dispatch("stash", { a: "x", b: "y" }));
console.log(dispatch("nope"));
const status = await probeFetch("https://rss.art19.com/the-allusionist");
console.log(`island fetch status ${status}`);
