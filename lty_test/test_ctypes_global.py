def main() -> None:
    import c_api_test

    c_api_test.reset_counter()
    print("before:", c_api_test.get_counter())
    c_api_test.add_to_counter(7)
    print("after +7:", c_api_test.get_counter())
    c_api_test.add_to_counter(-2)
    print("after -2:", c_api_test.get_counter())


if __name__ == "__main__":
    main()

