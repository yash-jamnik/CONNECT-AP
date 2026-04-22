import os
import tkinter as tk
from tkinter import filedialog, messagebox, ttk
from PIL import Image, ImageTk
import numpy as np


def generate_epd_array(image_path):
    image = Image.open(image_path).convert("RGB")
    epd_width, epd_height = 184, 360
    image = image.resize((epd_width, epd_height))

    image_array = np.array(image)
    epd_array = np.zeros((epd_height, epd_width), dtype=np.uint8)

    for y in range(epd_height):
        for x in range(epd_width):
            r, g, b = image_array[y, x]
            if r < 64 and g < 64 and b < 64:
                epd_array[y, x] = 0x00
            elif r > 200 and g > 200 and b < 64:
                epd_array[y, x] = 0x01
            elif r > 128 and g > 128 and b > 128:
                epd_array[y, x] = 0x03
            else:
                epd_array[y, x] = 0x02

    flat_epd = epd_array.flatten()
    packed_bytes = bytearray()

    for i in range(0, len(flat_epd), 4):
        pixels = flat_epd[i:i + 4]
        while len(pixels) < 4:
            pixels = np.append(pixels, 0x00)
        packed_bytes.append((pixels[0] << 6) | (pixels[1] << 4) | (pixels[2] << 2) | pixels[3])

    expected_size = (epd_width * epd_height) // 4
    if len(packed_bytes) != expected_size:
        raise AssertionError(
            f"Packed array size does not match expected size ({expected_size} bytes). Got {len(packed_bytes)} bytes."
        )

    hex_array = [f"0X{byte:02X}" for byte in packed_bytes]
    formatted_array = (
        f"const unsigned char battery_low_icon[{expected_size}]={{/*4-Gray picture*/\n"
        + ",\n".join(", ".join(hex_array[i:i + 16]) for i in range(0, len(hex_array), 16))
        + "\n}};"
    )

    return packed_bytes, formatted_array


class EPDArrayApp:
    def __init__(self, root):
        self.root = root
        self.root.title("EPD Array Generator")
        self.root.geometry("520x520")
        self.root.resizable(False, False)

        main_frame = ttk.Frame(root, padding=16)
        main_frame.pack(fill="both", expand=True)

        title = ttk.Label(main_frame, text="EPD Image to Binary Generator", font=("Segoe UI", 16, "bold"))
        title.pack(pady=(0, 12))

        self.preview_label = ttk.Label(main_frame, text="No image selected", anchor="center", relief="solid")
        self.preview_label.pack(fill="both", pady=(0, 12), ipadx=4, ipady=4)

        controls = ttk.Frame(main_frame)
        controls.pack(fill="x", pady=(0, 10))

        self.upload_button = ttk.Button(controls, text="Select Image", command=self.upload_image)
        self.upload_button.grid(row=0, column=0, padx=4, pady=4, sticky="ew")

        self.rotate_label = ttk.Label(controls, text="Rotate:")
        self.rotate_label.grid(row=0, column=1, padx=(12, 4), pady=4, sticky="e")

        self.rotation_var = tk.StringVar(value="0")
        self.rotate_entry = ttk.Entry(controls, textvariable=self.rotation_var, width=6)
        self.rotate_entry.grid(row=0, column=2, padx=4, pady=4)

        self.rotate_button = ttk.Button(
            controls, text="Rotate & Regenerate", command=self.rotate_image, state="disabled"
        )
        self.rotate_button.grid(row=0, column=3, padx=4, pady=4, sticky="ew")

        controls.columnconfigure(0, weight=1)
        controls.columnconfigure(3, weight=1)

        self.save_button = ttk.Button(main_frame, text="Save C Array", command=self.save_array, state="disabled")
        self.save_button.pack(fill="x", pady=(8, 4))

        self.status_label = ttk.Label(main_frame, text="Ready", relief="sunken", anchor="w")
        self.status_label.pack(fill="x", pady=(4, 0))

        self.image_path = None
        self.current_image = None
        self.epd_bytes = None
        self.epd_array_formatted = None

    def upload_image(self):
        file_path = filedialog.askopenfilename(
            filetypes=[("Image files", "*.jpg;*.jpeg;*.png;*.bmp")],
            title="Choose an image file",
        )
        if not file_path:
            return

        self.image_path = file_path
        self.current_image = Image.open(self.image_path).convert("RGB")
        self.display_preview(self.current_image)
        self.rotate_button["state"] = "normal"
        self.generate_and_save_output()

    def rotate_image(self):
        if not self.current_image:
            return

        try:
            angle = int(self.rotation_var.get())
        except ValueError:
            messagebox.showerror("Invalid Input", "Please enter a valid integer for rotation angle.")
            return

        self.current_image = self.current_image.rotate(angle, expand=True)
        self.display_preview(self.current_image)
        self.generate_and_save_output()

    def display_preview(self, image):
        preview = image.copy()
        preview.thumbnail((480, 280))
        self.preview_image = ImageTk.PhotoImage(preview)
        self.preview_label.configure(image=self.preview_image, text="")

    def generate_and_save_output(self):
        temp_path = "_temp_epd_image.png"
        self.current_image.save(temp_path)
        self.epd_bytes, self.epd_array_formatted = generate_epd_array(temp_path)

        output_bin_path = os.path.join(os.getcwd(), "batman.bin")
        with open(output_bin_path, "wb") as output_file:
            output_file.write(self.epd_bytes)

        self.save_button["state"] = "normal"
        self.status_label.configure(text=f"Saved batman.bin ({len(self.epd_bytes)} bytes)")
        messagebox.showinfo(
            "Success",
            f"Image processed successfully and written to:\n{output_bin_path}",
        )

    def save_array(self):
        if not self.epd_array_formatted:
            return

        file_path = filedialog.asksaveasfilename(
            defaultextension=".txt",
            filetypes=[("Text files", "*.txt"), ("C files", "*.c")],
            title="Save formatted array",
        )
        if not file_path:
            return

        with open(file_path, "w", encoding="utf-8") as f:
            f.write(self.epd_array_formatted)

        messagebox.showinfo("Saved", f"C array text saved to:\n{file_path}")


if __name__ == "__main__":
    root = tk.Tk()
    app = EPDArrayApp(root)
    root.mainloop()