describe("correct behavior", () => {
    test("basic functionality", () => {
        const duration = new Temporal.Duration(0, 0, 0, 0, 0, 0, 0, 0, 123);
        expect(duration.microseconds).toBe(123);
    });
});

describe("errors", () => {
    test("this value must be a Temporal.Duration object", () => {
        expect(() => {
            Reflect.get(Temporal.Duration.prototype, "microseconds", "foo");
        }).toThrowWithMessage(TypeError, "Not a Temporal.Duration");
    });
});
